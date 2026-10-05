# Home-hosted Bitwarden design plan

Date: 2026-10-05

Status: Planning; deployment has not started.

## Goal

Run a Bitwarden-compatible password vault on home infrastructure, accessible from Windows, macOS, and iOS devices, with two-step login using Google Authenticator.

The deployment serves one account for the owner's personal use across these devices.

## Decisions and proposed defaults

| Item | Status | Direction |
| --- | --- | --- |
| Hosting location | Decided | Home hosting |
| Host platform | Decided | Windows 11 home PC running VMware Workstation; currently version 15.5.7 |
| Linux guest | Decided | Ubuntu Server 24.04 LTS x86_64, terminal-only; no desktop GUI |
| Client platforms | Decided | Windows, macOS, and iOS |
| Backend ownership | Decided | Self-hosted server |
| Usage scope | Decided | Personal use only; one account |
| Remote access | Decided | Private HTTPS through Tailscale; access restricted to the owner's authorized devices |
| Two-step login | Requested capability | Support Google Authenticator using TOTP; enable it on each account |
| Server implementation | Recommended default | Official Bitwarden Lite |
| Deployment | Recommended default | Docker Compose on an always-on Linux host |
| Database | Recommended default | SQLite for the single personal account |
| HTTPS proxy | Decided | Tailscale Serve with trusted HTTPS; Caddy is not required for this endpoint |
| Client distribution | Recommended default | Standard Bitwarden apps configured with the self-hosted URL |
| Account email and notification inbox | Decided | Personal @outlook.com account |
| Outbound email delivery | Decided architecture; vendor selection deferred | Dedicated transactional SMTP service delivering to the Outlook inbox |
| Backup destination and method | Decided | Existing Google Drive subscription; locally encrypted, dated backups uploaded with rclone |
| Backup schedule and retention | Decided | Nightly and before upgrades; 7 daily, 4 weekly, and 6 monthly recovery points |
| Recovery targets and testing | Decided | At most 24 hours of data loss; restore within 24 hours with hardware and backup access available; test before production and every 3 months |

Home hosting, single-account personal use, and private HTTPS through Tailscale are confirmed. The recommended defaults above are a proposed baseline, rather than separately confirmed choices. Business use would require revisiting the deployment variant.

## Proposed architecture

```mermaid
flowchart TD
    Clients["Windows / macOS / iOS / browser extensions"]
    Clients -->|"HTTPS over private Tailscale network"| Proxy["Tailscale Serve in Ubuntu VM"]
    Proxy -->|"Loopback-only upstream"| Server["Bitwarden Lite"]
    Server --> Storage["Persistent volume: SQLite, attachments, configuration"]
    Server --> SMTP["External SMTP service"]
    Server --> Push["Bitwarden push relay, if enabled"]
    Storage --> Backup["Encrypted offsite backups"]
```

Use one stable Tailscale HTTPS address across all clients, such as `https://vault.<tailnet-name>.ts.net`. This is an illustrative hostname; the actual machine and tailnet names will be selected during setup. A custom domain and a public home IP are not required for the selected access method.

### Home host

- Run a Linux VM on the existing Windows 11 home PC using VMware Workstation.
- Recommend upgrading Workstation 15.5.7 to a maintained version that supports the installed Windows 11 release before production use. Confirm guest compatibility with the selected VMware release.
- Use Ubuntu Server 24.04 LTS x86_64 without a desktop environment. Enable OpenSSH Server for remote administration and retain the VMware console for setup and troubleshooting.
- Start with 2 virtual CPUs, approximately 2 GB RAM, and a 30 GB virtual disk as practical headroom, rather than minimum requirements. Increase storage for attachments and backup staging.
- Enable automatic startup after reboot, container restart policies, operating system security updates, and time synchronization.
- Disable Windows sleep/hibernation while the server is expected to be available. Test VM and application recovery after Windows reboot or updates.
- Store application data on the Linux guest filesystem. Use guest-aware backups; VMware snapshots are supplementary and do not replace offsite backups.
- Keep application data on persistent storage. Consider a UPS if home power interruptions are frequent.

### Application and database

- Run the official `ghcr.io/bitwarden/lite` image with a pinned version.
- Use SQLite initially to avoid operating a separate database service for a small number of users.
- Mount `/etc/bitwarden` persistently and identify any additional configured attachment or data paths before implementing backups.
- Obtain the installation ID and key required by the official deployment.
- Keep deployment configuration in a separate server deployment directory or repository. This clients repository contains client applications, not the backend implementation.
- Treat PostgreSQL as an option if future usage or operational requirements justify it.

### HTTPS and remote access

Private HTTPS through Tailscale Serve is selected for this single-user deployment.

- Install Tailscale in the Ubuntu guest and on Windows, macOS, and iOS.
- Enable the required Tailscale DNS and HTTPS settings and configure Serve to proxy to Bitwarden's local upstream.
- Publish the container upstream only on Ubuntu loopback, for example `127.0.0.1:<port>`, and keep the database private.
- Restrict tailnet access to the owner's authorized devices. Retain Bitwarden two-step login independently of network access.
- Keep the vault endpoint private; do not enable Tailscale Funnel or forward the vault's ports on the home router.
- Use private Tailscale connectivity for SSH administration as well.
- Test notification connections and login/sync from all clients over the selected HTTPS endpoint.

Tailscale introduces an external coordination dependency. The owner does not regularly use another VPN on iPhone. Configure and test on-demand connectivity on both Wi-Fi and cellular; another active VPN can conflict with Tailscale on iOS. Keep Tailscale account recovery available outside the vault and plan server device-key expiry so it does not unexpectedly interrupt access.

Use Bitwarden's own authentication. An additional interactive proxy login page can prevent native clients from connecting.

## Authentication

Use a strong, unique master password and enable authenticator-based two-step login on every account. Google Authenticator supplies the TOTP codes; this requires no custom client implementation.

Enrollment procedure:

1. Open the self-hosted web vault.
2. Navigate to **Settings → Security → Two-step login**.
3. Select **Authenticator App → Manage**.
4. Scan the QR code with Google Authenticator.
5. Enter the generated six-digit code to enable the method.
6. Record the recovery code outside the vault.
7. Keep the current web session open while testing a fresh login on another session or device.
8. Verify login from Windows, macOS, and iOS.

Two-step login applies to account sign-ins. Unlocking an already signed-in client is a separate operation and does not necessarily request a TOTP code. Remembered devices can also affect when prompts appear.

Keep the authenticator available independently of the vault it protects. Store recovery materials securely so they remain accessible if the phone is lost or the server is unavailable.

## Email and mobile synchronization

- Use the owner's personal @outlook.com address for the Bitwarden account and notification destination.
- Send account verification and security notifications through a dedicated transactional SMTP service. Select the vendor during implementation and confirm its approval, sender verification, authentication, and sending limits before deployment.
- Configure a sender identity authorized by that service; the recipient's Outlook address does not have to be the sender. Confirm whether the selected vendor requires a custom sending domain, since the private Tailscale endpoint does not provide one.
- Store SMTP credentials outside version control. Test delivery to the Outlook inbox, including spam-folder checks.
- Outlook SMTP authentication and an OAuth bridge are not part of the selected design. Google Authenticator codes remain independent of email delivery.
- Retain Bitwarden push relay support if automatic mobile synchronization is desired. This introduces an outbound dependency on Bitwarden's relay service.
- Disabling push relay leaves manual synchronization available but affects automatic mobile behavior.
- Validate synchronization between desktop and iOS clients after deployment.

## Backups and recovery

Accepted recovery objectives are a maximum of 24 hours of data loss and restoration within 24 hours, assuming replacement hardware and backup access are available. Validate the restoration target with a restore test before production use. The data-loss target depends on successful nightly backup uploads.

- Back up the database, attachments, configuration, and required server keys together.
- For SQLite, use a database-aware backup or briefly stop the application before copying persistent data. Avoid relying on an ordinary copy of an actively changing database.
- Create consistent, dated backups locally, encrypt them before upload, and use rclone to copy them to a dedicated folder in the existing Google Drive subscription. Use rclone crypt for client-side encryption, including filenames, with recovery configuration stored securely outside the vault.
- Preserve multiple recovery points rather than mirroring the live database. The backup folder is separate from application storage; Google Drive is not the live database filesystem.
- Run backups every night and retain 7 daily, 4 weekly, and 6 monthly recovery points.
- Alert when a backup or upload fails. Take an additional backup after important password changes or a large import.
- Keep backup decryption credentials accessible independently of Bitwarden.
- Test restoration on a separate host or isolated VM before production use and every 3 months. Verify download, decryption, database and attachment restoration, client login, and synchronization.
- Take a backup before every application upgrade. Database migrations may require restoring that backup to roll back safely.

## Operations

- Monitor service health, disk space, certificate renewal, and backup success.
- Apply server updates deliberately, with a backup and a client login/sync check afterward.
- Restrict registrations after onboarding if the selected server configuration supports the desired policy.
- Keep administrative access private and protect deployment secrets from version control.
- Document the server hostname, storage paths, deployment commands, backup process, and restore procedure.

## Client strategy

Use the official Windows, macOS, and iOS apps and browser extensions. Configure their self-hosted server URL before signing in. Self-hosting alone does not require rebuilding the clients.

The macOS app can also be built from this repository if customization is later required. That is a separate workstream involving npm dependencies, Rust native modules, and signing/notarization for distribution. No client build has been completed during this planning work.

## Resolved questions

- **#1 — Host and operating system — closed:** Use the existing Windows 11 home PC with VMware Workstation and an Ubuntu Server 24.04 LTS x86_64 guest, terminal-only with no desktop GUI. VMware is currently version 15.5.7; upgrading it and allocating VM resources remain implementation tasks.
- **#2 — Usage scope and account count — closed:** One account, exclusively for the owner's personal use across Windows, macOS, and iOS devices.
- **#3 — Remote access — closed:** Private HTTPS through Tailscale Serve. Install Tailscale on the Ubuntu VM and each client; keep the vault restricted to authorized devices. The owner does not regularly use another VPN on iPhone.
- **#5 — Email and offsite backups — closed at the design level:** Use the personal @outlook.com account for login and receiving notifications, a dedicated transactional SMTP service for outbound email, and the existing Google Drive subscription for encrypted, dated rclone backups. Selecting and verifying the SMTP vendor remains an implementation task; no vendor has been selected yet.
- **#6 — Recovery and retention — closed:** Accept up to 24 hours of data loss and restoration within 24 hours, assuming hardware and backup access are available. Back up nightly and before upgrades; retain 7 daily, 4 weekly, and 6 monthly recovery points. Test restoration before production and every 3 months; alert on backup failures.

## Open questions

Question numbers are retained from the original plan.

- **#4:** Select the Tailscale machine name and resulting HTTPS hostname during setup. A custom domain, public home IP, and dynamic DNS are no longer prerequisites; ISP reachability is relevant only if connectivity troubleshooting is needed.
- **#7:** Are any paid Bitwarden features needed? Confirm their self-hosted licensing requirements before deployment.

## Implementation sequence

1. Select the Tailscale hostname and SMTP vendor, confirm sender requirements, and resolve licensing choices.
2. Upgrade VMware to a suitable maintained release, allocate VM resources, install Ubuntu Server with OpenSSH, and prepare Docker Compose.
3. Create deployment configuration with pinned images and persistent storage.
4. Install Tailscale on the VM and clients, configure private HTTPS through Serve and access rules, then configure SMTP and optional push relay connectivity.
5. Create accounts and enroll Google Authenticator two-step login.
6. Configure all clients and test login, vault edits, attachments if used, and synchronization.
7. Implement encrypted offsite backups and complete a restore test.
8. Record maintenance and recovery procedures before storing production credentials.

## References

- [Official Bitwarden Lite deployment](https://bitwarden.com/help/install-and-deploy-lite/)
- [Configure clients for a self-hosted server](https://bitwarden.com/help/change-client-environment/)
- [Authenticator two-step login](https://bitwarden.com/help/setup-two-step-login-authenticator/)
- [Two-step login recovery code](https://bitwarden.com/help/two-step-recovery-code/)
- [Push relay configuration](https://bitwarden.com/help/configure-push-relay/)
- [Server backup guidance](https://bitwarden.com/help/backup-on-premise/)
- [Vaultwarden alternative](https://github.com/dani-garcia/vaultwarden)
- [Docker Engine installation on Ubuntu](https://docs.docker.com/engine/install/ubuntu/)
- [Alpine release and package support policy](https://alpinelinux.org/releases/)
- [Docker on Alpine](https://wiki.alpinelinux.org/wiki/Docker)
- [VMware Workstation host support matrix](https://knowledge.broadcom.com/external/article?legacyId=80807)
- [Tailscale Serve](https://tailscale.com/docs/features/tailscale-serve)
- [Tailscale firewall and NAT traversal](https://tailscale.com/kb/1181/firewalls)
- [Tailscale VPN On Demand for iOS and macOS](https://tailscale.com/docs/features/client/ios-vpn-on-demand)
- [Tailscale compatibility with other VPNs](https://tailscale.com/docs/reference/faq/other-vpns)
- [Caddy automatic HTTPS](https://caddyserver.com/docs/automatic-https)
- [Outlook.com SMTP authentication and settings](https://support.microsoft.com/en-us/outlook/pop-imap-and-smtp-settings-for-outlook-com)
- [Bitwarden SMTP configuration](https://bitwarden.com/help/smtp-configurations/)
- [rclone Google Drive backend](https://rclone.org/drive/)
- [rclone client-side encryption](https://rclone.org/crypt/)

Vaultwarden was considered as an unofficial compatible alternative. Official Bitwarden Lite remains the recommended baseline for this plan.
