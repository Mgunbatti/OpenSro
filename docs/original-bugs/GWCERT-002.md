# GWCERT-002: certification records are lost on failed logins and failed relays

- Original: `CUserCertifier_OverlapJob::OnCertifyComplete` (0x40daf0),
  `CUserCertifier_OverlapJob::ForwardLoginToAgentServer` (0x40d710),
  `CGatewayMainProcess::OnMsg_6323_CaptchaAnswer` (0x406910), GatewayServer
- Rewrite: `CUserCertifier_OverlapJob::OnFail` / `OnComplete` in
  `JMX_CommonServers/GatewayServer/UserCertifier.cpp`, `CGatewayMainProcess::OnMsg_6323` in
  `JMX_CommonServers/GatewayServer/GatewayMainProcessCert.cpp`
- Severity: medium (memory growth)

## What the original does

Every login takes a 0x290-byte `sCertifyParam` from its pool. It goes back through
`CertParam_Release` when the login ends. These paths set the job's or handler's pointer to NULL
without releasing it:

- `OnCertifyComplete` for every handler result other than 1 (no account, blocked, query error,
  ...): the client gets 0xA102 and the record is dropped.
- `OnCertifyComplete` with result 1 and `ForwardLoginToAgentServer` when the relay to the
  GlobalManager fails.
- The wrong captcha answer when the relay of 0x6111 fails (the session also stays open, while the
  right answer closes it).

## Effect

Each such login leaks one pooled record (and its age check record for contents 2 and 24). The
pool keeps allocating new blocks of 500 records; a server that sees many failed logins
(password guessing) grows without limit.

## Fix

Call `CertParam_Release` wherever the pointer is cleared without being handed to a relay.
