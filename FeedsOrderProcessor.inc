<?php
/**
 * @file
 */

/**
 * 
 */
class FeedsOrderProcessor extends FeedsProcessor {
  /**
   * Implementation of FeedsProcessor::process().
   */
  public function process(FeedsImportBatch $batch, FeedsSource $source) {
    // Count number of created and updated nodes.
    $created  = $updated = $failed = 0;

    while ($item = $batch->shiftItem()) {
      // Map item to a term.
      $order = $this->map($batch);

      $result_rs = db_query('SELECT nid, vid FROM {uc_products} WHERE model=' . $order->sku);
      $result = db_fetch_object($result_rs);
      
      $node = node_load($result->nid);
      $product = uc_product_load($result);
      
      if (!$product)
        $error_msg = 'There was an error finding the free trial product. Please contact an administrator.';
      $product->nid = $node->nid; //we need to do this in order for the file downloads to work
      $product->qty = $order->quantity;
      $product->title = $node->title;
      $product->price = $order->cost;

      if (!$order->user_id) {
        $last_name = $order->billing_last_name;
        $first_names = $order->billing_first_name;
        $email = $order->billing_email;
        $company = $order->billing_company;
      } 
      else {
        $account = user_load($order->user_id);
        $aid = _uc_addresses_get_default_address_id($account->uid);
        $address = _uc_addresses_db_get_address($account->uid, $aid);
        $first_names = $address->first_name;
        $last_name = $address->last_name;
        $email = $account->mail;
        $company = $address->company;
      }
      // Populate the order
      $order->products[] = $product;
      $order->payment_method = 'bank_transfer';
      $order->primary_email = $email;
      $order->billing_first_name = $first_names;
      $order->billing_last_name = $last_name;
      $order->billing_company = $company;
      if (!$order->user_id)
        $order->data['new_user']['pass'] = $form_state['values']['password'];
       
      // Complete the sale
      uc_order_save($order);
      uc_cart_complete_sale($order, TRUE);

      //order comment
      uc_order_comment_save($order->order_id, $order->uid, $order->order_comments);

      //integration with uc_pane_webform for card message
      if (function_exists('_uc_webform_pane_save') && $this->config['card_form_id']) {
        _uc_webform_pane_save($this->config['card_form_id'], $order->order_id, array('greeting_message' => $order->card_message));
      }

      //integrate delivery timeslot
      if (function_exists('uc_deliverytimeslot2_order')) {
        $order->deliverytimeslot['start_time'] = strtotime($order->delivery_date .' 09:00');
        $order->deliverytimeslot['end_time'] = strtotime($order->delivery_date .' 18:00');
        uc_deliverytimeslot2_order('save', $order, array());
      }
      
      uc_payment_enter($order->order_id, 'bank_transfer', 0, 0, NULL, t('Checkout completed for a free order.'));
    }
  }

  /**
   * Execute mapping on an item.
   */
  protected function map(FeedsImportBatch $batch, $target_order = NULL) {
    // Prepare user account object.
    if (empty($target_order)) {
      $order = uc_order_new($user->uid, 'completed');
    }

    // Have parent class do the iterating.
    return parent::map($batch, $order);
  }

  /**
   * Set target element.
   */
  public function setTargetElement(&$target_item, $target_element, $value) {
    $target_item->$target_element = $value;
  }

  /**
   * Return available mapping targets.
   */
  public function getMappingTargets() {
    $targets = array(
      'sku' => array(
        'name' => t('Product SKU'),
        'description' => t('SKU of the product.'),
      ),
      'quantity' => array(
        'name' => t('Quantity'),
        'description' => t(''),
      ),
      'cost' => array(
        'name' => t('Cost'),
        'description' => t(''),
      ),
      'delivery_date' => array(
        'name' => t('Delivery date'),
        'description' => t(''),
      ),
      'card_message' => array(
        'name' => t('Card message'),
        'description' => t(''),
      ),
      'order_comments' => array(
        'name' => t('Order Comments'),
        'description' => t(''),
      ),
      'delivery_first_name' => array(
        'name' => t('Delivery: First name'),
        'description' => t(''),
      ),
      'delivery_last_name' => array(
        'name' => t('Delivery: Last name'),
        'description' => t(''),
      ),
      'delivery_company' => array(
        'name' => t('Delivery: Company name'),
        'description' => t(''),
      ),
      'delivery_street1' => array(
        'name' => t('Delivery: Floor'),
        'description' => t(''),
      ),
      'delivery_street2' => array(
        'name' => t('Delivery: Building Street'),
        'description' => t(''),
      ),
      'delivery_phone' => array(
        'name' => t('Delivery: Phone'),
        'description' => t(''),
      ),
      'billing_first_name' => array(
        'name' => t('Billing: First name'),
        'description' => t(''),
      ),
      'billing_last_name' => array(
        'name' => t('Billing: Last name'),
        'description' => t(''),
      ),
      'billing_company' => array(
        'name' => t('Billing: Company name'),
        'description' => t(''),
      ),
      'billing_street1' => array(
        'name' => t('Billing: Floor'),
        'description' => t(''),
      ),
      'billing_street2' => array(
        'name' => t('Billing: Building Street'),
        'description' => t(''),
      ),
      'billing_phone' => array(
        'name' => t('Billing: Phone'),
        'description' => t(''),
      ),
      'billing_email' => array(
        'name' => t('Billing: Email'),
        'description' => t(''),
      ),
      'user_id' => array(
        'name' => t('User ID (optional)'),
      ),
    );
    return $targets;
  }

  /**
   * Implementation of FeedsProcessor::clear().
   */
  public function clear(FeedsBatch $batch, FeedsSource $source) {
    // Do not support deleting orders as we have no way of knowing which ones we
    // imported.
    throw new Exception(t('User processor does not support deleting orders.'));
  }

  /**
   * Override parent::configDefaults().
   */
  public function configDefaults() {
    return array(
      'card_form_id' => '',
      'mappings' => array(),
    );
  }

  public function configForm(&$form_state) {
    $form = array();
    $form['card_form_id'] = array(
      '#type' => 'textfield',
      '#title' => t('Card webform node ID'),
      '#default_value' => $this->config['card_form_id'],
    );
    return $form;
  }

}