<?php

/**
 * An interface for pluggable mail back-ends.
 */
class CiviCRMMailerMailSystem implements MailSystemInterface {

  /**
   * Format a message composed by drupal_mail() prior sending.
   *
   * @param $message
   *   A message array, as described in hook_mail_alter().
   *
   * @return
   *   The formatted $message.
   */
  public function format(array $message) {
    $mailclass = variable_get('civicrmmailer_engine', 'DefaultMailSystem');
    $default = new $mailclass();
    return $default->format($message);
  }

  /**
   * Send a message composed by drupal_mail().
   *
   * @param $message
   *   Message array with at least the following elements:
   *   - id: A unique identifier of the e-mail type. Examples: 'contact_user_copy',
   *     'user_password_reset'.
   *   - to: The mail address or addresses where the message will be sent to.
   *     The formatting of this string must comply with RFC 2822. Some examples:
   *     - user@example.com
   *     - user@example.com, anotheruser@example.com
   *     - User <user@example.com>
   *     - User <user@example.com>, Another User <anotheruser@example.com>
   *    - subject: Subject of the e-mail to be sent. This must not contain any
   *      newline characters, or the mail may not be sent properly.
   *    - body: Message to be sent. Accepts both CRLF and LF line-endings.
   *      E-mail bodies must be wrapped. You can use drupal_wrap_mail() for
   *      smart plain text wrapping.
   *    - headers: Associative array containing all additional mail headers not
   *      defined by one of the other parameters.  PHP's mail() looks for Cc
   *      and Bcc headers and sends the mail to addresses in these headers too.
   *
   * @return
   *   TRUE if the mail was successfully accepted for delivery, otherwise FALSE.
   */
  public function mail(array $message) {
    civicrm_initialize();

    $addresses = Mail_RFC822::parseAddressList($message['to']);

    foreach ($addresses as $a) {
      $recipient = $a->mailbox . '@' . $a->host;
      $this->mailSingle($message, $recipient);
    }

    return TRUE;
  }

  private function mailSingle(array $message, string $recipient) {
    $contact = NULL;

    // Drupal user emails are unique, so fetch the Drupal record
    // then we'll fetch the civicrm contact record.
    $uid = db_query('SELECT uid FROM {users} WHERE mail = :mail', [
      ':mail' => $recipient,
    ])->fetchField();

    if ($uid) {
      try {
        $uf = civicrm_api3('UFMatch', 'getsingle', [
          'uf_id' => $uid,
        ]);

        $contact = civicrm_api3('Contact', 'getsingle', [
          'id' => $uf['contact_id'],
        ]);
      }
      catch (Exception $e) {
        // The uf_match might not exist yet.
        // For example: user account creation through a CiviCRM profile.
        // This is OK, the code below will fetch by email.
      }
    }

    // The user might not exist. It could be a webform email.
    // Find a matching email in CiviCRM, prioritize primary emails.
    if (empty($contact)) {
      $dao = CRM_Core_DAO::executeQuery('SELECT contact_id FROM civicrm_email WHERE email = %1 ORDER BY is_primary DESC', [
        1 => [$recipient, 'String'],
      ]);

      if ($dao->fetch()) {
        $contact = civicrm_api3('Contact', 'getsingle', [
          'id' => $dao->contact_id,
        ]);
      }
    }

    // FIXME: maybe return FALSE? for now, we prefer to get notifications
    // when it fails.
    if (empty($contact)) {
      throw new Exception("Could not find Drupal user for {$recipient}");
    }

    $formattedContactDetails = [];
    $formattedContactDetails[] = $contact;

    // By default Drupal sends text emails
    // Should we try to detect if the message is already in HTML?
    $html_message = nl2br($message['body']);

    // Fetch the default organisation for the domain
    $domain_id = CRM_Core_Config::domainID();

    $default_org_id = CRM_Core_DAO::singleValueQuery('SELECT contact_id FROM civicrm_domain WHERE id = %1', [
      1 => [$domain_id, 'Positive'],
    ]);

    list($sent, $activityId) = CRM_Activity_BAO_Activity::sendEmail(
      $formattedContactDetails,
      $message['subject'],
      $message['body'], // text message
      $html_message, // html body
      NULL,
      $default_org_id // used for the "from"
    );

    return TRUE;
  }

}
