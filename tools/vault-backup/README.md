# Bitwarden Vault Backup & Recover

This native Win32 C tool provides manual backups, end-to-end VM vault recovery, a decrypt-only mode, backup history across folders, and local location management. The owner drags encrypted files into Google Drive manually; the program has no cloud sign-in, upload, synchronization, scheduling, or automatic deletion. Its window and desktop shortcut are named Bitwarden Vault Backup & Recover; executable names and the existing state directory remain compatible.

## Build and run

Requires Windows 11, system OpenSSH, and MinGW-w64 GCC / windres. No 7-Zip, rclone, or third-party runtime DLLs are required.

```powershell
& .\tools\vault-backup\build.ps1
& .\tools\vault-backup\bin\vault-backup.exe
```

Specify the compiler with `build.ps1 -Gcc '<full-path-to-gcc.exe>'`. The GCC specs file overrides only this build's endfile to avoid conflicts between the WinLibs default manifest and the application manifest; it does not modify the toolchain.

The GUI, CLI, application diagnostics, and new history statuses use US English. The language indicator shows "English (US)"; legacy language settings no longer select Chinese. Existing Chinese history statuses and application diagnostics are displayed in English without rewriting stored records. Raw diagnostics from external tools retain their original text. The initial default folder is `VaultBackups` in Windows Documents. Click "Add folder" to select another local fixed/removable drive folder, up to 220 UTF-16 characters; UNC and mapped network drives are unsupported. Location changes are saved immediately; removing a location removes only its configuration, not files or history. Avoid shared or automatically cloud-synchronized folders for temporary data. Application staging always uses a dedicated LocalAppData directory.

The compact window has a default client area of 860 × 520 logical pixels and a minimum of 760 × 440; physical pixels follow monitor DPI. It supports resizing, maximizing, and restoring. Path fields expand with width, the history table expands with height, and bottom actions remain near the bottom edge. Horizontal scrolling provides access to complete paths, SHA256, and details.

The GUI palette matches the illustrated scroll icon: warm ivory background, pale celadon title and status areas, a vermilion "Back up now" button, and fine gold lines. Input areas and buttons are clearly separated, and history uses alternating pale rows. Buttons retain keyboard focus indicators. If Windows high-contrast mode is enabled at startup, system controls and colors are used. Windows 11 also uses a celadon title bar; older systems retain the system title bar.

Per-Monitor DPI Awareness V2 draws fonts, icons, and controls at actual DPI on 4K and other high-resolution displays, avoiding Windows bitmap stretching. Moving between monitors with different scaling adjusts layout, table column widths, and fonts. Segoe UI uses ClearType; body and title fonts are 13 and 19 logical pixels and scale with DPI.

## Private server configuration

Copy `server.example.ini` to `server.local.ini` in `tools/vault-backup`, then set
`ssh_target`, `host_key_alias`, and `vault_url` for your deployment. The local file
is ignored by Git; the example contains only reserved example addresses.
Use an HTTPS origin without a trailing slash for `vault_url`. Keep the file in
the parent directory of `bin`, regardless of the process working directory.
The GUI recovery confirmation, SSH transport, and recovered-vault browser link
read these values at runtime. Missing or invalid configuration prevents remote
backup/recovery; decrypt-only operation does not require server configuration.

The same ignored file holds machine-specific Windows paths in `[paths]`.
Set `ssh_key` and `known_hosts` for the initial GUI defaults and the CLI engine
verification command. Existing GUI-saved SSH settings still override these
defaults. Without path defaults, the GUI fields start empty and can be filled
manually. Set `gcc` for builds and test scripts; an explicit `-Gcc` argument
takes precedence, and `gcc.exe` on PATH is the fallback when `gcc` is unset.
The shared `config.ps1` reader never executes configuration values as code.
Paths with spaces need no quotation marks inside the INI file.

The remaining path entries (`user_profile`, `vm_root`, `vm_config`, `vmrun`,
`dhcp_leases`, `vm_media`, `first_boot_log`, and `certificate_helper`) record
local operations paths. They are not used by the application. Public examples
use corresponding angle-bracket placeholders rather than personal paths.

On the VM, install the same file at `/etc/vault-backup/server.local.ini` before
installing the updated `vm/vault-recover.py`. The recovery helper reads its HTTPS
origin from this file before changing the deployment. Keep the file root-owned,
mode 0600, in a root-owned directory that other users cannot write. For example,
upload it to a private temporary path, then run:

```bash
sudo install -d -o root -g root -m 0700 /etc/vault-backup
sudo install -o root -g root -m 0600 /home/vaultadmin/server.local.ini /etc/vault-backup/server.local.ini
rm /home/vaultadmin/server.local.ini
```

The optional `[network]` section in the owner's local file records addresses
referenced by the operations guide; the application does not use that section.
Angle-bracket values in this README and the operations guide are placeholders
and must be replaced before running example commands. Do not commit the local
file or use `git add -f` on it.

SSH configuration:

- Target: `vaultadmin@<vault-tailscale-ip>`, `HostKeyAlias=<vm-lan-ip>`.
- Private key: `<ssh-key>`.
- known_hosts `<known-hosts>`。
- `BatchMode=yes`, strict host key verification, connection timeouts, and keepalive. Interactive passwords and unknown host keys are rejected. Select alternative key/known_hosts files in the GUI.
- Backups require the VM and Bitwarden to be running. VM recovery requires a running VM reachable by SSH; the recovery helper starts Docker and the restored container without changing boot startup policy.

Enter and confirm an independent strong password of at least 12 characters each time. Input fields are cleared when backup begins; the password in job memory is erased when processing ends. The program does not save passwords or pass them on the command line. **Store the password outside this Bitwarden instance** so it remains available if the service fails.

Click "Back up now" to export, encrypt, decrypt for SHA256 comparison, copy to the destination, and verify in a background thread. Output is `vault-<UTC-time>-<random-id>.tar.zst.vbk`. Duplicate operations, normal window-close requests, and system session-end requests are rejected during backup; forced process termination and power loss cannot be prevented.

Backup and GUI recovery use a determinate 0%→100% progress bar, with the percentage also shown in the status text. Progress measures completed stages and processed bytes within a stage, not elapsed or remaining time. Remote backup export occupies 0%→30%; SSH export has no total-size protocol, so it stays at 0% until the complete stream is received and checked, then reaches 30%. Encryption occupies 30%→50%, decryption verification 50%→80%, destination copying 80%→90%, and destination verification 90%→98%; only final saving and history recording reach 100%. Decrypt-only mode advances to 95% by authenticated plaintext bytes written. VM recovery uses 0%→40% for authenticated decryption, 45% for SSH transfer, 55% when the remote file is received, 65% after archive/SQLite validation, 70% when the image is ready, 75% when replacement begins, 85% after data installation, 90% for service checks, and 98% after private HTTPS/API checks pass. It reaches 100% only after the remote success receipt and local plaintext cleanup. Transfer, image downloads, and service startup have no total-size protocol and retain the current stage percentage. Key derivation and file flushing retain the current value; timers do not fabricate progress. Success retains 100%, the next task starts at 0%, and failures never display 100%. Refreshing labels, resizing, and DPI changes preserve progress.

Success and failure records are stored in `%LOCALAPPDATA%\VaultBackup\history.ini`; settings use `settings.ini` in the same folder. Both are UTF-16. History displays UTC time, byte count, path, SHA256, and details; double-click for details. "Open backup folder" opens the selected history file's folder, or the current backup location if no record is selected or it has no file path, making manual Google Drive uploads convenient. Missing, inaccessible, or unopenable folders produce a message. History is not deleted automatically and does not prove a file still exists or has been uploaded.

The "Size" column selects B, KB, MB, GB, TB, PB, or EB using powers of 1024. B is an integer; other units use two decimal places. For example, 94635 bytes displays as 92.42 KB. Hovering over the cell shows the full original byte count; history still stores the original integer.

Select a history record and click "Delete record" or press Delete in the list for two confirmations, both defaulting to "No". The first shows the time, path, and an unchecked "Also delete the backup file from disk (permanently)" option; records without a file path omit the option. The second explicitly confirms record-only deletion or permanent deletion of the `.vbk` file without using the Recycle Bin. Canceling either confirmation leaves the record and file intact. With the option unchecked, only the list entry and `history.ini` section are removed. With it checked, the corresponding file is deleted first, then the record; directories are never deleted recursively. Missing files allow record cleanup. In-use files, insufficient permissions, invalid paths, directories, and reparse points retain the record and report an error. If file deletion succeeds but history writing fails, the program reports partial completion and retains the record for retry. Other records referencing the same file remain but lose access to it. Success and failure records can both be deleted; deletion is disabled without a selection or while backup/recovery is running.

Existing backups are never overwritten. A unique `.partial` is created in the destination folder, verified and flushed, then renamed atomically in that folder. Leftover `.partial` files are not completed backups. Disconnected drives, full disks, and permission errors report failure. SHA256 checks file transfer integrity; authenticated decryption verifies the encrypted container.

## VM export helper

### Backup failure when Docker is stopped

After VM reboot, Docker service/socket do not start automatically under the existing operations policy, and Bitwarden uses `restart: "no"`. Backup requires them to be running. Manually run `sudo systemctl start docker` and `sudo docker start bitwarden` in the VM, then wait for private HTTPS to return before backing up. Boot startup policy does not need to change.

The helper reports an unreachable Docker daemon, stopped/paused containers, failure stages, exit codes, and timeouts instead of only `CalledProcessError`. Diagnostics do not print raw subprocess stderr, deployment environment, or configuration contents.

Installed at `/usr/local/sbin/vault-backup`, root:root, mode 0755, with no arguments and a fixed scope. Invoke it with `sudo -n /usr/local/sbin/vault-backup`. After changing the source, install it:

```powershell
scp.exe -i <ssh-key> -o BatchMode=yes -o HostKeyAlias=<vm-lan-ip> -o StrictHostKeyChecking=yes -o UserKnownHostsFile=<known-hosts> .\tools\vault-backup\vm\vault-backup.py vaultadmin@<vault-tailscale-ip>:/home/vaultadmin/vault-backup-upload.py
ssh.exe -i <ssh-key> -o BatchMode=yes -o HostKeyAlias=<vm-lan-ip> -o StrictHostKeyChecking=yes -o UserKnownHostsFile=<known-hosts> vaultadmin@<vault-tailscale-ip> 'sudo install -o root -g root -m 0755 /home/vaultadmin/vault-backup-upload.py /usr/local/sbin/vault-backup && rm /home/vaultadmin/vault-backup-upload.py'
```

The helper checks container state, fixed data mounts, and deployment paths, holding `/run/vault-backup.lock` to prevent concurrency. It pauses the container and copies the complete `data`, `settings.env`, and `compose.yaml` into a tar snapshot. The `finally` block checks actual state and retries unpause a limited number of times. It then checks SQLite integrity on the isolated snapshot (including WAL), adds a manifest, and runs `zstd -19 --single-thread` and `zstd -t`. **The container has resumed before compression and transfer.** Ultra compression is not used. Stdout contains only compressed binary data; stderr contains only nonsecret status.

Service freezes briefly during the snapshot; duration depends on data size and disk performance. `freeze_seconds` in `manifest.json` measures from pause initiation through the final resume check. Tar times out after 300 seconds, individual Docker operations after 30 seconds, and resume is attempted up to three times. Normal backup pause/unpause does not implement unattended restart recovery. Docker service/socket remain disabled at boot and the container retains `restart: "no"`.

VM plaintext staging uses `/var/backups/bitwarden/manual-*` (0700). Windows compressed plaintext and verification staging use `%LOCALAPPDATA%\VaultBackup\staging`, with an ACL restricted to the current user and SYSTEM. Normal completion and handled failures clean up files; power loss, SIGKILL, and forced termination can leave sensitive files. Ordinary deletion does not guarantee secure erasure on disks/SSDs. The VM needs space for tar, isolated databases, and compressed files. Windows staging needs space for compressed plaintext, the encrypted copy, and verification output; the destination also needs space for the final encrypted file.

After an unexpected interruption, first check state through the VMware console or SSH:

```bash
sudo docker inspect --format '{{.State.Running}} {{.State.Paused}}' bitwarden
# Only if the existing container is paused:
sudo docker unpause bitwarden
```

Then verify private HTTPS. If the VM or Docker is stopped, start it manually as described in the operations document. Confirm no backup process is running before manually deleting staging leftovers. A `.partial` or test package with an unknown password is not a recoverable backup.

## Decryption and recovery

`.vbk` is a custom authenticated container that ordinary archive tools cannot open directly. Retain the CLI and this format specification, and perform an isolated restore drill before production use.

### End-to-end GUI VM recovery

1. Start the VM manually and confirm Tailscale/SSH connectivity. Configure the SSH key and known_hosts in backup mode; recovery uses the same settings.
2. Select "Recovery" mode and the original `.tar.zst.vbk` file, then enter its password once. "Restore the Bitwarden vault on the VM" is selected by default.
3. Click "Recover" and confirm the displayed backup path and target VM `<vault-tailscale-ip>`. This temporarily stops the current service and restores the vault to the backup time; newer additions and edits will not appear in the restored vault.
4. The GUI authenticates and decrypts in a dedicated `%LOCALAPPDATA%\VaultBackup\Recovered-<id>` folder, then transfers through SSH stdin with strict host key verification to the root recovery helper. The password is neither transferred nor saved, and its memory is erased immediately after decryption. No plaintext is uploaded to the cloud, and Windows extraction tools are not required.
5. The helper first validates zstd, archive paths/types, the manifest, SQLite integrity, and an image digest from a fixed source. Path traversal, duplicate paths, symbolic/hard links, and devices are rejected. Archived Compose files are not executed. Missing images are pulled first; pull failure does not stop the old service.
6. The helper shares `/run/vault-backup.lock` with backups, retains the old deployment at `/opt/bitwarden-before-recover-*`, stops and temporarily renames the old container, restores all data/configuration/keys, sets data ownership to `911:911`, and recreates `bitwarden` with fixed Docker arguments. It publishes only `127.0.0.1:8080`, limits memory to 1536 MiB, rotates logs, and uses `restart=no`. It starts the container and restores the fixed Tailscale Serve private HTTPS mapping; Docker service/socket boot settings remain unchanged. Success removes the stopped old container but retains its deployment data. The new container has Compose project/service labels for later maintenance recreation.
7. The GUI shows 100% and "VM restored" only when the new container is running/unpaused, certificate-verified private HTTPS `/api/config` returns the version and correct vault URL, the client receives a success receipt, and local plaintext cleanup completes. "Open recovery result" opens the private vault. The owner must still sign in to verify items, attachments, TOTP, and client synchronization; the application does not request the master password for automatic login.

For handled replacement/startup/health-check failures, the helper removes the new container, moves the failed deployment to a protected diagnostics folder, restores the old deployment and container name, and restores its original running/paused state. `/var/backups/bitwarden/recover-*/status.json` records the stage, old directory/container name, and original state. After SSH disconnects, the remote transaction continues attempting completion or rollback; the GUI does not claim success from disconnection. Power loss, SIGKILL, disk failures, or unavailable Docker may prevent rollback and require journal inspection. An unfinished journal blocks another automatic restore. Old data, failed deployments, and journals contain sensitive information and are not deleted automatically. If the journal is `complete-old-container-retained`, handle the old container before Compose maintenance.

The VM must have root-owned `/usr/local/sbin/vault-recover` installed (no arguments, mode 0755), with permission for the SSH user to invoke it through `sudo -n`. Install `vm/vault-recover.py` using the export helper's scp + `sudo install` procedure, together with the private server configuration described above. Recovery does not create/reinstall the VM OS or SSH/Tailscale identity, or restore cloud files. Download the encrypted backup locally first.

### GUI decrypt-only mode

Uncheck VM restoration, select an existing local destination folder, and enter the password. Output is `Recovered-<id>/vault.tar.zst`, protected by a Windows ACL. This mode does not connect to the VM. It checks authentication and the zstd header only; validate the complete compressed stream, archive, SQLite, and service restoration manually below. Avoid cloud-synchronized folders for sensitive plaintext. Backup history is not modified. Incorrect passwords, corruption, or write failures clean up this operation's temporary output; cleanup failures display the leftover folder.

### CLI recovery

Run in an **interactive Windows console**; the password does not appear in command arguments, pipes, or files:

```powershell
& .\tools\vault-backup\bin\vault-backup-cli.exe decrypt 'D:\VaultBackups\vault-example.tar.zst.vbk' 'D:\PrivateRestore\vault.tar.zst'
```

The CLI reads the password without echo and requires a nonexistent output file. Authentication failures, truncation, appended data, and write failures remove incomplete output. Successful output is sensitive compressed plaintext containing the database, configuration, and server keys. Restrict the Windows recovery folder ACL to the current user and SYSTEM beforehand, and avoid cloud-synchronized folders.

Transfer `.tar.zst` to a mode-0700 directory on an isolated Ubuntu host and keep the file mode 0600. Install zstd, tar, and Python3 there; Windows does not need these extraction tools. On the isolated host:

```bash
umask 077
mkdir -m 700 restore
zstd -t vault.tar.zst
zstd -d vault.tar.zst -o vault.tar
tar -tf vault.tar > archive-list.txt
# This tool's archive should contain only bitwarden/ data/configuration and manifest.json.
# Check for unexpected absolute paths, .., symbolic/hard links, devices, and other entries first:
tar -xf vault.tar -C restore --no-same-owner
python3 - <<'PY'
import sqlite3
from pathlib import Path
p = Path('restore/bitwarden')
assert all((p / name).is_file() for name in ('data/vault.db', 'settings.env', 'compose.yaml'))
with sqlite3.connect(p / 'data/vault.db') as db:
    assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
print('SQLite and required file checks passed')
PY
```

Extraction restores files only. Full acceptance requires the backup's pinned image, configuration, keys, and complete data directory on an isolated VM, with UID/GID **911:911**. Preserve file permissions: deployment directory 0700, configuration 0600. Check version and time against the manifest. Do not combine SQLite WAL/SHM with a database from another time. Use a separate endpoint/port restricted to local or private access, avoiding the live `/opt/bitwarden`, original Tailscale Serve mapping, and production container. Start the isolated service manually and verify account login, TOTP, client synchronization, existing attachments, and server keys before planning production disaster recovery. Complete replacement and manual recovery still need separate acceptance; these tool tests did not recreate the production container.

Back up manually before maintenance or upgrades. Daily backups and manual Google Drive uploads are recommended; confirm upload completion, retain multiple recovery points, and drill every 3 months. Successful daily backups and uploads are prerequisites for a 24-hour data-loss target; the application does not guarantee it automatically. After downloading a copy, compare its SHA256 with history, then decrypt and verify.

## VBKZST01 format

All header integers are little-endian. The body stores no separate chunk lengths; derive them from total header length and the fixed chunk size.

| Byte offset | Length | Contents |
| --- | --- | --- |
| 0 | 8 | ASCII `VBKZST01` |
| 8 | 16 | BCrypt cryptographically random salt |
| 24 | 8 | BCrypt cryptographically random nonce prefix |
| 32 | 4 | PBKDF2 iterations: writes 600000; reads accept 600000–2000000 |
| 36 | 8 | Total compressed plaintext bytes (nonzero, at most 1 TiB) |

Passwords are UTF-8 without a terminating NUL or Unicode normalization. PBKDF2-HMAC-SHA256 derives a 32-byte AES key. Each chunk of at most 1 MiB uses AES-256-GCM independently; a 16-byte authentication tag follows its ciphertext. The nonce consists of an 8-byte random prefix plus a 4-byte **big-endian**, zero-based chunk index. AAD contains the complete 44-byte header, a 4-byte little-endian index, and a 4-byte little-endian current plaintext length. Every chunk authenticates the header; total length, order, and lengths are authenticated. The final chunk length follows from remaining bytes. EOF is required afterward, rejecting truncation, reordering, header tampering, and appended data.

## Validation and limitations

```powershell
& .\tools\vault-backup\bin\vault-backup-cli.exe --selftest "$env:LOCALAPPDATA\VaultBackup\tests space unicode"
```

Self-tests cover more than two chunks and a nonaligned tail, Unicode passwords, incorrect passwords, tampering, truncation, appended data, header authentication, no overwriting of existing output, and SHA256 verification. `tests/test_helper.py <helper-path>` uses fake Docker/file paths for snapshot failures, uncertain pause results, interruptions, and resume retries without modifying production containers. `tests/validate_restore.py` is this deployment's isolated validation script; it does not print configuration, keys, or database contents.

`--test-backup <private-local-directory>` is for development validation only. A random password exists only in memory; it uses a real VM export and verification, also producing sensitive `restore-test.tar.zst`. History uses a separate test state directory. Delete test packages, compressed plaintext, and test state afterward; these are not usable owner backups.

Run `tests/run-tests.ps1` for Windows build and regression checks. Validate GUI interaction, scaling, accessibility, manual CLI input, and full restored-service login on your deployment before production use. Keep detailed deployment, validation, and machine performance records locally; these records are excluded from Git. A production recoverable backup requires an owner-entered and safely retained password.

## Desktop shortcut and icon

Run `install-shortcut.ps1` to create `Bitwarden Vault Backup & Recover.lnk` on the current user's desktop, targeting the built GUI and using `assets/vault-backup-heian.ico`. After verifying the new shortcut, it removes only the old `Vault Backup.lnk` and `Vault Backup & Recover.lnk` that target this installation. Double-click the shortcut to start without administrator privileges.

```powershell
& .\tools\vault-backup\install-shortcut.ps1
```

The window, taskbar, and shortcut use a Heian-era illustrated scroll and golden cloud icon with a pale celadon background and transparent rounded corners, without a cypress fan motif. The ICO contains 9 sizes from 16 to 256 pixels. Icon resources are embedded during building without extra runtime dependencies. `assets/vault-backup-heian-source.png` is the source image; run `assets/generate-icon.py` with Python/Pillow only when regenerating the ICO.
