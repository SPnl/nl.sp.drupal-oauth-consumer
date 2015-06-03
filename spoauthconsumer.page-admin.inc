<?php

/**
 * Admin settings form
 *
 * @param mixed $form Form
 * @param mixed $form_state Form state
 *
 * @return array Form elements
 */
function spoauthconsumer_admin_settings_form($form, &$form_state) {

  // Formulier
  $form = array(
    '#theme' => 'system_settings_form',
  );

  $form['spoauthconsumer_description'] = array(
    '#markup' => '<h3>Instellingen</h3><p>Instellingen om deze Drupal-site te laten werken als OAuth2 Consumer.</p>',
  );
  $form['spoauthconsumer_enable']      = array(
    '#type'          => 'checkbox',
    '#title'         => 'OAuth-login inschakelen',
    '#default_value' => variable_get('spoauthconsumer_enable', true),
    '#description'   => 'Schakelt de OAuth-loginfunctie in of uit.',
    '#required'      => false,
  );

  $form['spoauthconsumer_server'] = array(
    '#type'          => 'textfield',
    '#title'         => 'Server',
    '#default_value' => variable_get('spoauthconsumer_server', 'https://spnettest.sp.nl'),
    '#description'   => 'URL van de provider-installatie, zonder pad.',
    '#required'      => true,
  );

  $form['spoauthconsumer_client_id'] = array(
    '#type'          => 'textfield',
    '#title'         => 'Client-ID',
    '#default_value' => variable_get('spoauthconsumer_client_id'),
    '#description'   => 'De ID en secret zoals je deze op de provider-installatie hebt ingevuld.',
    '#required'      => true,
  );

  $form['spoauthconsumer_client_secret'] = array(
    '#type'          => 'textfield',
    '#title'         => 'Client-secret',
    '#default_value' => variable_get('spoauthconsumer_client_secret'),
    '#required'      => true,
  );

  $form['spoauthconsumer_roles'] = array(
    '#prefix' => '<div id="spoauthconsumer_roles">',
    '#suffix' => '</div>',
  );

  // Get options and default values
  if (variable_get('spoauthconsumer_server')) {
    $roleOptions = spoauthconsumer_admin_get_remote_roles();
    $roleStorage = variable_get('spoauthconsumer_roles_storage');

    $loginSelected = array();
    $syncSelected = array();
    foreach($roleStorage as $srole) {
      if($srole['allow_login'] == true)
        $loginSelected[] = $srole['id'];
      if($srole['sync'] == true)
        $syncSelected[] = $srole['id'];
    }
  } else {
    $roleOptions = array('[Sla de instellingen op om de rollen op te halen.]');
  }

  $form['spoauthconsumer_roles_allow_login'] = array(
    '#type'        => 'checkboxes',
    '#options'     => $roleOptions,
    '#default_value' => $loginSelected,
    '#title'       => 'Rollen die kunnen inloggen',
    '#description' => 'Vink de rollen op de provider-site aan die kunnen inloggen op deze website.',
  );

  $form['spoauthconsumer_roles_sync'] = array(
    '#type'        => 'checkboxes',
    '#options'     => $roleOptions,
    '#default_value' => $syncSelected,
    '#title'       => 'Rollen die gesynchroniseerd worden',
    '#description' => 'Vink de rollen op de provider-site aan die gesynchroniseerd moeten worden met gelijknamige rollen op deze website.',
  );

  $form['actions'] = array(
    '#type' => 'actions',
  );

  $form['actions']['submit'] = array(
    '#type'  => 'submit',
    '#value' => 'Instellingen opslaan',
  );

  return $form;
}

/**
 * Submits settings form
 * @param mixed $form Form
 * @param mixed $form_state Form state
 * @return bool Success
 */
function spoauthconsumer_admin_settings_form_submit($form, &$form_state) {

  form_state_values_clean($form_state);

  // Set OAuth base variables
  variable_set('spoauthconsumer_enable', $form_state['values']['spoauthconsumer_enable']);
  variable_set('spoauthconsumer_server', $form_state['values']['spoauthconsumer_server']);
  variable_set('spoauthconsumer_client_id', $form_state['values']['spoauthconsumer_client_id']);
  variable_set('spoauthconsumer_client_secret', $form_state['values']['spoauthconsumer_client_secret']);

  // Update role storage
  $roleStorage = variable_get('spoauthconsumer_roles_storage', array());
  foreach($roleStorage as &$srole) {

    // Set if this role should be allowed to log in
    if(in_array($srole['id'], $form_state['values']['spoauthconsumer_roles_allow_login'])) {
      $srole['allow_login'] = true;
    } else {
      $srole['allow_login'] = false;
    }

    // Set if this role should be synced with local roles
    if(in_array($srole['id'], $form_state['values']['spoauthconsumer_roles_sync'])) {
      $srole['sync'] = true;
    } else {
      $srole['sync'] = false;
    }

    // Create the local role if it doesn't exist, or delete local id if it isn't synced anymore
    if(($srole['allow_login'] || $srole['sync']) && !isset($srole['local_id'])) {

      $existing = user_role_load_by_name($srole['name']);
      if($existing) {
        $srole['local_id'] = $existing->rid;
      } else {

        $local_role       = new \StdClass;
        $local_role->name = $srole['name'];
        user_role_save($local_role);

        $srole['local_id'] = $local_role->rid;
      }

    } elseif(isset($srole['local_id'])) {
      unset($srole['local_id']);
    }
  }

  variable_set('spoauthconsumer_roles_storage', $roleStorage);

  // drupal_set_message('<pre>' . print_r($roleStorage, true) . '</pre>');
  drupal_set_message('De instellingen zijn opgeslagen');
  return true;
}

/**
 * Fetch roles from remote site and update local role storage
 * @return array Roles
 */
function spoauthconsumer_admin_get_remote_roles() {

  try {
    // Fetch roles via API
    $oauth2 = _spoauthconsumer_load_oauth2();
    $res    = $oauth2['client']->fetch($oauth2['apiEndpoint'] . '/roles');

    // Parse roles
    $xml   = new \SimpleXMLElement($res['result']);
    $roleOptions = array();
    if ($xml->roles) {
      foreach ($xml->roles->item as $role) {
        $roleOptions[(string)$role->id] = (string)$role->name;
      }
    }

    // Update local role storage (used to check which roles have to be added/removed for a user)
    if (count($roleOptions) > 0) {

      $roleStorage = variable_get('spoauthconsumer_roles_storage');
      foreach ($roleOptions as $id => $name) {
        if(!array_key_exists($id, $roleStorage))
          $roleStorage[$id] = array();
        $roleStorage[$id]['id'] = $id;
        $roleStorage[$id]['name'] = $name;
        $roleStorage[$id]['deleted'] = false;
      }

      foreach ($roleStorage as &$srole) {
        if (!array_key_exists($srole['id'], $roleOptions)) {
          $srole['deleted'] = true;
        }
      }

      variable_set('spoauthconsumer_roles_storage', $roleStorage);
    }

  } catch (\Exception $e) {
    $roleOptions = array('[Kon rollen niet ophalen]');
  }

  return $roleOptions;
}
