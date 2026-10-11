# Rebuild or migrate the private Bitwarden server to a new Windows PC

Baseline: October 11, 2026. This runbook reproduces the current architecture:
Windows + VMware Workstation, Ubuntu 24.04 LTS, official Bitwarden Lite with
SQLite, private Tailscale HTTPS, and manual encrypted backups. Docker and
Bitwarden start automatically **after the VM powers on**. Windows does not
automatically power on the VM.

Use route A to move the actual server, including its SSH and Tailscale identity.
Use route B if the old VM is unavailable or you deliberately want a clean OS.
Both routes finish at section 9. A fresh deployment without a backup creates an
empty vault; repository source alone cannot recover existing accounts or items.

The guide was checked against the current files, recovery implementation, and
official documentation. A second-PC rebuild has not been executed. Matching
VMware versions does not guarantee matching behavior on different Windows
builds, CPUs, or Hyper-V/VBS configurations. Treat the acceptance checklist as
the completion gate rather than assuming a successful installation is enough.

## 1. Preserve the recovery kit before replacing the old PC

Keep the following in protected storage outside this Bitwarden instance:

| Item | Why it is needed |
| --- | --- |
| Current repository snapshot, including local source changes | Rebuild the Windows tool and install the matching VM helpers |
| Latest successful `.tar.zst.vbk`, its SHA256, and backup password | Recover the deployment; the password is neither in Git nor stored by the tool |
| Master password, authenticator access, and two-step recovery code | Sign in to the recovered account |
| Tailscale account access and its MFA/recovery material | Join new devices and edit access policy |
| `tools/vault-backup/server.local.ini` | Actual server addresses and machine-specific paths; ignored by Git |
| SSH private key and trusted `known_hosts` | Administer the old/copied VM; never publish the private key |
| Bitwarden installation ID/key | Required for a new empty deployment; existing backup includes `settings.env` |
| Fully shut-down VM directory, if available | Route A; includes disks, firmware state, SSH keys, Tailscale state, and vault data |
| Original Ubuntu VMDK plus checksum manifest, if available | Reproduce the original base image rather than a newer rolling download |
| Original installer/toolchain versions or offline installers | Future availability and package repositories can change |

Also record the old vault HTTPS origin, VM LAN address, Windows NAT address,
Windows Tailscale IP, VM Tailscale IP, host-key alias, and existing tailnet policy.
Use the private INI, not this public guide, for real addresses.

The historical `self-hosted-bitwarden-phase-1-vm-operations.md`, design plan,
tool `TEST_RESULTS.md`, and `PERFORMANCE.md` are Git-ignored. An ordinary clone
does not include them. This runbook is intended to be versioned. Preserve a
source snapshot with uncommitted changes as well; cloning an older commit will
not reproduce fixes that were never committed. The VM disk and decrypted
backups contain secrets even if no passwords appear in filenames.

## 2. Prepare Windows and fill in your migration worksheet

1. Install the intended VMware Workstation version and confirm its tools include
   `vmrun.exe` and `vmware-vdiskmanager.exe`.
2. Install Windows OpenSSH Client, Git, and Tailscale. Sign Windows into the same
   owner account. Record its **new** Tailscale IP using `tailscale ip -4`.
3. Retrieve the repository snapshot and recovery kit. Use a local directory with
   English filenames. Install MinGW-w64 GCC with `windres.exe` to build the
   backup tool. Python 3 and `pycdlib` are needed only for route B's seed ISO.
4. In VMware's Virtual Network Editor, record VMnet8's NAT subnet and the
   Windows VMnet8 adapter IP. Use `ipconfig` to cross-check. Do not assume they
   match the old PC. Leave unrelated VMs/networks unchanged.

Example PowerShell variables for later Windows commands; replace the example
paths and addresses with your values. Run Windows blocks from this repository's
root. Run Bash blocks inside the new/copied Ubuntu VM.

```powershell
$RepoRoot = (Get-Location).Path
$VmRoot = 'D:\VaultVM'
$VmConfig = Join-Path $VmRoot 'vault.vmx'
$Vmrun = 'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
$Vdiskmanager = 'C:\Program Files (x86)\VMware\VMware Workstation\vmware-vdiskmanager.exe'
$Key = Join-Path $env:USERPROFILE '.ssh\vault_vm_ed25519'
$KnownHosts = Join-Path $VmRoot 'known_hosts'
$LanIp = '192.0.2.10'
$HostKeyAlias = $LanIp
$VaultIp = '100.64.0.10'
$VaultUrl = 'https://vault.example.ts.net'
$SshOptions = @('-i', $Key, '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
    '-o', "HostKeyAlias=$HostKeyAlias", '-o', "UserKnownHostsFile=$KnownHosts")
```

Do not execute examples with these reserved/example values. For route A, retain
the existing host-key alias if you retain the old known_hosts entry. The LAN IP,
SSH alias, Tailscale IP, and HTTPS hostname are different concepts.
Rebuild `$SshOptions` after changing the key, alias, or known-hosts path; the
array contains the values from when it was created.

The current server baseline is 1 vCPU, 2 GB RAM, a 30 GB disk, EFI, an LSI Logic
SCSI disk controller, and an E1000 NAT adapter. Workstation 15.5.7 used virtual
hardware version 16. Its two-vCPU configuration crashed on the old host; even
one vCPU crashed during an in-guest reboot on October 11. Use graceful shutdown
and a subsequent power-on for acceptance. Do not silently disable Windows
security/Hyper-V or upgrade VM hardware to work around a crash; investigate
compatibility separately. The known crash is not fixed by this runbook.

## 3A. Route A: move the fully shut-down VM

1. On the old server, finish any backup/recovery operation and make a fresh
   password-protected backup. Confirm success and copy its encrypted file and
   SHA256 off the old PC. Keep its password separately.
   **Before shutdown**, add SSH firewall allowances for the new Windows NAT
   adapter IP and new Windows Tailscale IP, using section 5's commands and the
   old guest's actual NAT interface. Update the administrator grant in section
   6 as well. This avoids lockout if VMnet8 uses a different subnet on the new
   PC. The administrator is password-locked, so a normal console login is not
   a fallback. If the old VM is already inaccessible, use an authenticated
   offline Linux recovery procedure to repair its firewall, or use route B;
   do not assume moving the disk removes its existing restrictions.
2. Shut down Ubuntu cleanly:

   ```bash
   sudo shutdown -h now
   ```

3. Confirm VMware shows **Powered off**, not Suspended. Copy the complete VM
   directory to protected storage, then to `$VmRoot` on the new PC. Include every
   referenced VMDK/extent, `.vmx`, `.nvram`, and seed ISO. If disks are stored
   outside that directory or snapshots exist, include the entire referenced
   disk/snapshot chain. Do not copy a live disk or copy/delete `.lck` files as a
   substitute for shutting down. Retain the original copy for rollback.
4. Open the copied `.vmx`. Correct any absolute disk/ISO paths if needed. When
   VMware asks whether it was moved or copied, select **I moved it** for this
   single replacement server. Never run the old and moved VM simultaneously:
   they share vault data and Tailscale machine identity.
5. Power on the new VM. Obtain its DHCP address from VMware Tools, VMware NAT
   leases, or the console. Its LAN IP may differ. Confirm the preserved SSH
   fingerprint against the recovery kit; use the retained key and alias.
6. Keep the VM console available. Add an SSH firewall allowance for the new
   Windows VMnet8 IP and, after Windows joins Tailscale, its new Tailscale IP.
   Follow section 5's discovery and firewall commands. Remove obsolete host
   allowances only after a second SSH session connects successfully.
7. Update tailnet grants/tests for the new Windows administrator IP using
   section 6. A moved VM normally retains its Tailscale identity and vault
   origin; confirm them rather than re-registering it unnecessarily.
8. Confirm Docker/Bitwarden boot settings using section 9. If the copied VM
   predates the autostart change, install the current helpers in section 7 and
   apply `enable-autostart.py` to the existing running container.
9. Continue with sections 8-10. Do **not** initialize an empty database or run
   restore over the moved server unless you intend to replace its data.

## 3B. Route B: create a clean Ubuntu cloud-image VM

Skip this section for route A. A new VM gets new SSH/Tailscale identities; old
fingerprints and addresses must not be blindly reused.

### Download and verify the base disk

Use the retained verified Ubuntu 24.04 amd64 cloud VMDK if available. The disk
originally used had SHA256
`58001251111ae414947c62799e5b74c522ecaa6fa4cd654f7846042a0c60cbd1`.
That hash applies to that retained file only; the release URL is rolling.

Otherwise download `ubuntu-24.04-server-cloudimg-amd64.vmdk`, `SHA256SUMS`, and
`SHA256SUMS.gpg` from the [official Ubuntu image directory](https://cloud-images.ubuntu.com/releases/noble/release/).
Install a trusted GnuPG distribution on Windows if needed. In the download
directory, verify the manifest using the key fingerprint published in
[Ubuntu's cloud-image verification guide](https://ubuntu.com/docs/public-images/public-images-how-to/verify-image-checksum/):

```powershell
gpg.exe --keyserver hkps://keyserver.ubuntu.com --recv-keys D2EB44626FDDC30B513D5BB71A5D6C4C7DB87C81
gpg.exe --list-keys --with-fingerprint D2EB44626FDDC30B513D5BB71A5D6C4C7DB87C81
gpg.exe --verify SHA256SUMS.gpg SHA256SUMS
if ($LASTEXITCODE -ne 0) { throw 'Ubuntu checksum signature verification failed.' }
```

Check the complete fingerprint and a good signature, not just the displayed
signer name. If Ubuntu rotates the signing key, confirm the new fingerprint
through its official documentation before trusting it. Then compare the disk:

```powershell
Get-FileHash -Algorithm SHA256 '<downloaded-vmdk>'
```

Match the manifest entry for that exact filename and download date. Stop if it
differs. Keep the original download read-only as a recovery artifact. Copy it
as `vault.vmdk` into a new, unused `$VmRoot`. While that disk is not open in
VMware, expand the working copy:

```powershell
& $Vdiskmanager -x 30GB (Join-Path $VmRoot 'vault.vmdk')
if ($LASTEXITCODE -ne 0) { throw 'Disk expansion failed.' }
```

### Create the SSH key and NoCloud seed

If the old administrator key is available and you want to retain it, protect
and use it. Otherwise create a new dedicated key at a path that does not already
exist. The current GUI uses `BatchMode=yes`, so an encrypted private key must
already be unlocked in a working SSH agent; otherwise use a dedicated key with
an empty passphrase and tightly restricted file permissions.

```powershell
New-Item -ItemType Directory -Force (Split-Path $Key) | Out-Null
ssh-keygen.exe -t ed25519 -f $Key -C 'vault-vm-admin'
$Account = [Security.Principal.WindowsIdentity]::GetCurrent().Name
icacls.exe $Key /inheritance:r /grant:r "${Account}:(R)"
```

Create `$VmRoot\seed\user-data` as UTF-8 without BOM and LF line endings. Replace
the public-key line with the complete contents of `$Key.pub`:

```yaml
#cloud-config
hostname: vault
manage_etc_hosts: true
ssh_pwauth: false
disable_root: true
timezone: Asia/Taipei
users:
  - name: vaultadmin
    gecos: Vault VM administrator
    groups: [adm, sudo]
    sudo: ['ALL=(ALL) NOPASSWD:ALL']
    shell: /bin/bash
    lock_passwd: true
    ssh_authorized_keys:
      - ssh-ed25519 REPLACE_WITH_YOUR_PUBLIC_KEY vault-vm-admin
package_update: false
package_upgrade: false
growpart:
  mode: auto
  devices: ['/']
resize_rootfs: true
runcmd:
  - [ssh-keygen, -lf, /etc/ssh/ssh_host_ed25519_key.pub]
```

This matches the current password-locked, key-only administrator with
passwordless sudo. Possession of the private key therefore grants root-level
administration. The seed contains only the public key, never the private key.

Create `seed\meta-data` with a unique instance ID for this **fresh disk**:

```yaml
instance-id: vault-new-pc-REPLACE_WITH_UNIQUE_ID
local-hostname: vault
```

Install `pycdlib` into a dedicated Windows Python virtual environment, then save
this code as `$VmRoot\create-seed.py`. It produces a NoCloud ISO with both
Rock Ridge and Joliet filenames. The volume label and filenames are required
by [cloud-init NoCloud](https://docs.cloud-init.io/en/latest/reference/datasources/nocloud.html).

```python
from pathlib import Path
import pycdlib

root = Path(__file__).resolve().parent
output = root / 'seed.iso'
if output.exists():
    raise SystemExit('seed.iso already exists; refusing to overwrite')
iso = pycdlib.PyCdlib()
iso.new(interchange_level=3, vol_ident='CIDATA', joliet=3, rock_ridge='1.09')
for name, iso_name in [('user-data', 'USERDATA'), ('meta-data', 'METADATA')]:
    iso.add_file(str(root / 'seed' / name), iso_path=f'/{iso_name}.;1',
                 rr_name=name, joliet_path='/' + name)
iso.write(str(output))
iso.close()
```

```powershell
python -m venv (Join-Path $VmRoot 'seed-tools')
& (Join-Path $VmRoot 'seed-tools\Scripts\python.exe') -m pip install pycdlib
& (Join-Path $VmRoot 'seed-tools\Scripts\python.exe') (Join-Path $VmRoot 'create-seed.py')
```

### Create the VM configuration and first boot

Save the following as `$VmConfig`. These are relative disk/ISO paths. Let VMware
generate fresh UUID/MAC values; do not paste the old server's UUID or MAC.

```text
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "16"
displayName = "vault"
guestOS = "ubuntu-64"
firmware = "efi"
memsize = "2048"
numvcpus = "1"
cpuid.coresPerSocket = "1"
scsi0.present = "TRUE"
scsi0.virtualDev = "lsilogic"
scsi0:0.present = "TRUE"
scsi0:0.fileName = "vault.vmdk"
ide1:0.present = "TRUE"
ide1:0.deviceType = "cdrom-image"
ide1:0.fileName = "seed.iso"
ide1:0.startConnected = "TRUE"
ethernet0.present = "TRUE"
ethernet0.connectionType = "nat"
ethernet0.virtualDev = "e1000"
ethernet0.addressType = "generated"
ethernet0.startConnected = "TRUE"
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "serial.log"
serial0.startConnected = "TRUE"
serial0.yieldOnMsrRead = "TRUE"
usb.present = "FALSE"
sound.present = "FALSE"
floppy0.present = "FALSE"
isolation.tools.copy.disable = "TRUE"
isolation.tools.paste.disable = "TRUE"
isolation.tools.dnd.disable = "TRUE"
sharedFolder.maxNum = "0"
tools.syncTime = "FALSE"
```

Open it in VMware and verify settings before powering on. Wait for cloud-init
to finish. Find the guest LAN IP via the console/VMware DHCP leases or:

```powershell
& $Vmrun -T ws getGuestIPAddress $VmConfig
```

Tools may not be installed yet, so this command can fail. Do not interpret that
alone as a failed boot. Record the guest's ED25519 fingerprint from the local
boot console or `serial.log`. If the fingerprint is unavailable, resolve that
through local console/boot diagnostics before trusting a network-scanned key.

Capture the candidate host key and compare its fingerprint to that trusted
local record, then use the file only if they match:

```powershell
ssh-keyscan.exe -t ed25519 $LanIp | Set-Content -Encoding ascii $KnownHosts
ssh-keygen.exe -lf $KnownHosts
$SshOptions = @('-i', $Key, '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
    '-o', "HostKeyAlias=$HostKeyAlias", '-o', "UserKnownHostsFile=$KnownHosts")
ssh.exe @SshOptions "vaultadmin@$LanIp"
```

`ssh-keyscan` alone does not authenticate the server. The old fingerprint is
expected to change for a fresh OS. Do not delete a mismatch without verifying
the new identity. Inside the VM, confirm `cloud-init status --wait`,
`df -h /`, `lsblk`, and `timedatectl status`. Resolve disk growth/cloud-init
errors before installing the vault.

## 4. Install guest software (route B)

Inside Ubuntu, first install prerequisites:

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates curl python3 tar zstd ufw open-vm-tools nano
sudo timedatectl set-timezone Asia/Taipei
sudo timedatectl set-ntp true
```

Use the [official Docker Ubuntu repository instructions](https://docs.docker.com/engine/install/ubuntu/):

```bash
sudo install -d -m 0755 /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
sudo tee /etc/apt/sources.list.d/docker.sources >/dev/null <<'EOF'
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: noble
Components: stable
Architectures: amd64
Signed-By: /etc/apt/keyrings/docker.asc
EOF
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
```

Use [Tailscale's official Ubuntu package repository](https://pkgs.tailscale.com/stable/):

```bash
sudo curl -fsSL https://pkgs.tailscale.com/stable/ubuntu/noble.noarmor.gpg -o /usr/share/keyrings/tailscale-archive-keyring.gpg
sudo curl -fsSL https://pkgs.tailscale.com/stable/ubuntu/noble.tailscale-keyring.list -o /etc/apt/sources.list.d/tailscale.list
sudo apt-get update
sudo apt-get install -y tailscale
sudo systemctl enable --now containerd.service docker.service docker.socket tailscaled.service
sudo docker version
sudo docker compose version
tailscale version
```

The original observed versions were Docker 29.9.0, Compose v5.6.0, and Tailscale
1.104.1. Installing from stable repositories later may select newer versions.
For exact package reproduction, preserve the corresponding packages and
dependency inventory; do not claim that unpinned package installation reproduces
every historical version. Review Ubuntu updates with `apt list --upgradable`
and apply them before accepting a new server; use shutdown/power-on if needed.

### Optional: downloads require the Windows proxy

Skip this if direct downloads work. If Windows already has a trusted HTTP proxy
listening on loopback, open a separate PowerShell window and hold this tunnel:

```powershell
ssh.exe @SshOptions -o ExitOnForwardFailure=yes -N -R 127.0.0.1:18080:127.0.0.1:10808 "vaultadmin@$LanIp"
```

Replace `10808` with that proxy's real port. For guest curl requests add
`--proxy http://127.0.0.1:18080`; for apt commands add both
`-o Acquire::http::Proxy=http://127.0.0.1:18080` and
`-o Acquire::https::Proxy=http://127.0.0.1:18080` before the operation name.
The proxy stays bound to loopback; closing the tunnel invalidates it.

Docker image pulls need daemon proxy configuration rather than just shell
variables. On a fresh VM only, create a root-owned drop-in at
`/etc/systemd/system/docker.service.d/temporary-download-proxy.conf`:

```ini
[Service]
Environment="HTTP_PROXY=http://127.0.0.1:18080"
Environment="HTTPS_PROXY=http://127.0.0.1:18080"
Environment="NO_PROXY=localhost,127.0.0.1"
```

Reload systemd and restart Docker, pull the required image, then remove **only**
this temporary drop-in, reload systemd, and restart Docker while no production
vault is running. Do not leave an enabled daemon pointing at a closed tunnel.
Certificate issuance/renewal also requires outbound connectivity; if blocked,
resolve it before accepting Serve. Do not disable TLS validation or assume a
one-time certificate workaround guarantees future renewal.

## 5. Configure SSH and the guest firewall

Discover the actual interface and addresses first:

```bash
ip -br address
ip route
```

Set these values in your current Bash session. Do not use the example values:

```bash
LAN_IF='REPLACE_WITH_NAT_INTERFACE'
HOST_NAT_IP='REPLACE_WITH_WINDOWS_VMNET8_IP'
ADMIN_TS_IP='REPLACE_WITH_NEW_WINDOWS_TAILSCALE_IP'
```

Keep the current session and VMware console available. Add the new rules before
removing obsolete ones. Use a second connection to verify access:

```bash
sudo tee /etc/ssh/sshd_config.d/00-vault-hardening.conf >/dev/null <<'EOF'
PermitRootLogin no
PasswordAuthentication no
KbdInteractiveAuthentication no
EOF
sudo sshd -t
sudo systemctl reload ssh
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow in on "$LAN_IF" from "$HOST_NAT_IP" to any port 22 proto tcp
sudo ufw allow in on tailscale0 from "$ADMIN_TS_IP" to any port 22 proto tcp
sudo ufw --force enable
sudo ufw status numbered
```

Do not open 8080 to the LAN or internet. Do not add the administrator to the
Docker group to bypass sudo. This deployment uses standard OpenSSH, not
Tailscale SSH.

## 6. Join Tailscale and restrict access

For a clean VM, authenticate under the same owner account:

```bash
sudo tailscale up --hostname=vault
tailscale ip -4
tailscale status
```

If the old server still exists, keep it powered off and plan the identity/name
cutover in the Tailscale admin console. A fresh node may get a suffix and a new
IP. Record the actual DNS name and update `$VaultIp`/`$VaultUrl`. Never assume
requesting `vault` recreates the old node or grants it the old hostname.

Enable MagicDNS and HTTPS certificates in the tailnet. For continuity with
existing clients/backups, route A is the simplest way to preserve the old
origin. With route B, confirm the intended final origin before restoring.

Save the current tailnet policy privately before editing it. Add/update these
restricted grants and tests, replacing every example. Remove broad overlapping
allow-all grants/ACLs that would still permit access to this VM. Preserve
necessary rules for other services rather than replacing the entire policy
blindly. [Tailscale grants are additive](https://tailscale.com/docs/reference/syntax/grants).

```json
{
  "hosts": {
    "vault-server": "100.64.0.10",
    "vault-windows-admin": "100.64.0.20"
  },
  "grants": [
    {"src": ["owner@example.com"], "dst": ["vault-server"], "ip": ["tcp:443"]},
    {"src": ["vault-windows-admin"], "dst": ["vault-server"], "ip": ["tcp:22"]}
  ],
  "tests": [
    {
      "src": "100.64.0.20",
      "accept": ["vault-server:22", "vault-server:443"],
      "deny": ["vault-server:80", "vault-server:8080"]
    },
    {
      "src": "100.64.0.30",
      "accept": ["vault-server:443"],
      "deny": ["vault-server:22", "vault-server:80", "vault-server:8080"]
    }
  ]
}
```

Replace `100.64.0.30` with another owner device's actual IP. This explicitly
tests denial of SSH from a device that still has HTTPS access. Save only after the
admin console's policy validation passes and a second Tailscale SSH connection
works. Assess node-key expiry and certificate renewal in the admin console;
record a maintenance plan rather than assuming permanent connectivity.

## 7. Install helpers and create or restore the vault

### Configure the Windows tool and trusted VM origin

Copy `tools/vault-backup/server.example.ini` to `server.local.ini`, or edit your
preserved local copy. Set the current SSH target, verified host-key alias, HTTPS
origin **without a trailing slash**, key/known_hosts paths, GCC path, and new VM
paths. The new Windows username/drive letters may differ. Do not commit this
file. Avoid moving old GUI `settings.ini` blindly because its saved key/host
paths override the new INI defaults; set the GUI fields explicitly.

On the VM, create a minimal trusted origin file (no Windows paths are needed):

```bash
sudo install -d -o root -g root -m 0700 /etc/vault-backup
sudo tee /etc/vault-backup/server.local.ini >/dev/null <<'EOF'
[server]
vault_url=https://REPLACE_WITH_FINAL_VAULT_HOSTNAME
EOF
sudo chmod 0600 /etc/vault-backup/server.local.ini
sudo install -d -o root -g root -m 0700 /var/backups/bitwarden
```

From Windows, upload the current source helpers over the verified SSH session:

```powershell
scp.exe @SshOptions .\tools\vault-backup\vm\vault-backup.py .\tools\vault-backup\vm\vault-recover.py .\tools\vault-backup\vm\enable-autostart.py "vaultadmin@${LanIp}:/home/vaultadmin/"
```

Inside the VM:

```bash
sudo install -o root -g root -m 0755 /home/vaultadmin/vault-backup.py /usr/local/sbin/vault-backup
sudo install -o root -g root -m 0755 /home/vaultadmin/vault-recover.py /usr/local/sbin/vault-recover
```

The seed administrator already has passwordless sudo. Confirm
`sudo -n true` works. Never print resolved Compose configuration,
`settings.env`, raw Docker environment, TOTP secrets, or installation keys in
logs/support messages.

### Route B with a valid existing backup

1. Build the Windows tool in section 8. Confirm its `server.local.ini`, GUI SSH
   fields, trusted server-origin file, tailnet grants, and HTTPS certificate
   prerequisites point at the replacement VM.
2. Download the encrypted backup locally and compare SHA256 to the saved
   successful history. Keep the backup password independent of the master
   password. Existing `.vbk` archives contain deployment configuration and
   keys, but not the VM OS, SSH identity, or Tailscale identity.
3. **Origin compatibility gate:** the archived `settings.env` must target the
   replacement's final HTTPS hostname. The recovery helper restores that file
   unchanged and checks the resulting API origin. Changing only the local INI
   does not rewrite the archived domain. If the origin changed, use decrypt-only
   recovery and the controlled manual procedure below; do not bypass health
   checks or repeatedly force the GUI recovery.
4. With an unchanged origin, open **Recover**, select the encrypted file, enter
   its password, enable VM restoration, and read the target confirmation before
   proceeding. This replaces the target deployment. On a clean VM, no empty
   account/database needs to be created first. Keep the SSH session/network
   alive and allow startup/cached-image download time.
5. The current helper validates archive paths, zstd, SQLite, and pinned image;
   restores all data/configuration/keys with UID/GID 911:911; regenerates safe
   Compose; starts `bitwarden` with `restart=always`; and checks private HTTPS.
   It retains old deployment data if present. See the
   [tool recovery documentation](../tools/vault-backup/README.md#end-to-end-gui-vm-recovery)
   for progress, cleanup, and rollback journal behavior.
6. Enable guest services using section 4 if not already enabled. The GUI recovery
   starts Docker but does not enable its boot settings. Continue with section 9.

### Route B when the HTTPS origin must change

Use **decrypt-only** to output an authenticated `vault.tar.zst` into a private
Windows folder. Transfer it into a mode-0700 private VM directory, set the file
to mode 0600, and follow the README's [decryption and restoration steps](../tools/vault-backup/README.md)
to validate zstd, reject unsafe tar entries, extract in isolation, and check
SQLite. On a **fresh VM with no `/opt/bitwarden` deployment**, install the
validated extracted deployment there, preserving all data and server keys.
Set data ownership recursively to 911:911; keep the deployment at 0700 and
`settings.env`/`compose.yaml` at 0600, root-owned.

After the isolated validation, use your actual private extraction path in place
of `/var/backups/bitwarden/manual-restore/extracted`. This block refuses an
existing target; it is only for a clean VM:

```bash
sudo test ! -e /opt/bitwarden || { echo 'Target deployment already exists; stop here.'; exit 1; }
sudo cp -a /var/backups/bitwarden/manual-restore/extracted/bitwarden /opt/bitwarden
sudo chown root:root /opt/bitwarden /opt/bitwarden/settings.env /opt/bitwarden/compose.yaml
sudo chmod 0700 /opt/bitwarden
sudo chmod 0600 /opt/bitwarden/settings.env /opt/bitwarden/compose.yaml
sudo chown -R 911:911 /opt/bitwarden/data
sudo nano /opt/bitwarden/settings.env
```

Edit only `BW_DOMAIN` in `settings.env` to the new hostname without `https://`.
Review other origin-specific settings privately if present. Keep the original
encrypted backup untouched. Replace Compose with section 7's fixed template
using the image digest from the validated manifest. Pull that exact image,
validate Compose quietly, start it, and configure Serve as below. Retain the
private extracted copy until acceptance; then remove only identified temporary
plaintext files. For an existing target, use a planned replacement with a fresh
backup and rollback copy; do not copy extracted files over a running database.
Reconfigure clients to the new origin and perform fresh login/sync tests.

### Route B with no existing vault: initialize an empty deployment

Skip this for route A and backup restoration. Obtain an installation ID/key
from [Bitwarden's installation page](https://bitwarden.com/host/) and keep them
private. The observed current image is:

```text
ghcr.io/bitwarden/lite@sha256:f42c5a256face5c59990d515e7d672ff4f029c6d03acb8f0cb0c5839cd27fe2a
```

Use the archive's manifest digest for restoration instead; do not select
`latest` during recovery. If the pinned image becomes unavailable, restore from
a preserved image export or plan and validate a compatible upgrade. A newer
database may not work with an older image.

```bash
sudo install -d -o root -g root -m 0700 /opt/bitwarden
sudo install -d -o 911 -g 911 -m 0700 /opt/bitwarden/data
sudo touch /opt/bitwarden/settings.env
sudo chmod 0600 /opt/bitwarden/settings.env
sudo nano /opt/bitwarden/settings.env
```

Enter the following privately; replace all placeholders before startup:

```ini
BW_DOMAIN=REPLACE_WITH_FINAL_VAULT_HOSTNAME
BW_DB_PROVIDER=sqlite
BW_DB_FILE=/etc/bitwarden/vault.db
BW_ENABLE_SSL=false
BW_INSTALLATION_ID=REPLACE_WITH_PRIVATE_INSTALLATION_ID
BW_INSTALLATION_KEY=REPLACE_WITH_PRIVATE_INSTALLATION_KEY
globalSettings__disableUserRegistration=false
```

TLS is terminated by Tailscale, while the container accepts only the loopback
upstream. These database/domain options follow the
[official Bitwarden Lite deployment](https://bitwarden.com/help/install-and-deploy-lite/).
SMTP is not configured in the current baseline; do not assume email delivery
or email-based recovery works.

For a fresh deployment or controlled manual restore, write this root-owned
`/opt/bitwarden/compose.yaml`, replacing the image with the validated digest:

```yaml
name: bitwarden
services:
  bitwarden:
    image: ghcr.io/bitwarden/lite@sha256:f42c5a256face5c59990d515e7d672ff4f029c6d03acb8f0cb0c5839cd27fe2a
    container_name: bitwarden
    env_file:
      - settings.env
    restart: always
    mem_limit: 1536m
    ports:
      - "127.0.0.1:8080:8080"
    volumes:
      - ./data:/etc/bitwarden
    logging:
      driver: json-file
      options:
        max-size: "10m"
        max-file: "3"
```

```bash
sudo chmod 0600 /opt/bitwarden/compose.yaml
sudo docker pull 'REPLACE_WITH_VALIDATED_IMAGE_DIGEST'
sudo docker compose -f /opt/bitwarden/compose.yaml config --quiet
sudo docker compose -f /opt/bitwarden/compose.yaml up -d --pull never
sudo tailscale serve --bg --https=443 http://127.0.0.1:8080
sudo tailscale serve status
```

[Serve requires HTTPS certificates](https://tailscale.com/docs/features/tailscale-serve)
and its `--bg` configuration persists across guest boots. Do not use Funnel.
Wait for HTTPS/API readiness. Create the owner's account, enroll authenticator
TOTP, store recovery codes externally, and test a fresh TOTP login. Then change
`globalSettings__disableUserRegistration=true` in the private env file and
recreate the container with `compose up -d --pull never`. Confirm the existing
account still works and `/api/config` reports registration disabled.

For route A's older existing deployment, run the uploaded autostart setup only
after Docker/Bitwarden are running and unpaused:

```bash
sudo python3 /home/vaultadmin/enable-autostart.py
```

It supports the current fixed Compose layout's old `restart: "no"` or new
`restart: always` setting. An unexpected layout is rejected instead of edited
blindly. A route B deployment created with the template/recovery helper already
has `always`; enabling services in section 4 completes boot configuration.

## 8. Build and configure the Windows backup/recovery tool

Set `[paths] gcc` to the new machine's actual MinGW-w64 compiler. From the
repository root:

```powershell
& .\tools\vault-backup\build.ps1
& .\tools\vault-backup\tests\run-tests.ps1
& .\tools\vault-backup\install-shortcut.ps1
```

The main GUI is `tools/vault-backup/bin/vault-backup.exe`; the CLI is alongside
it. The desktop shortcut is **Bitwarden Vault Backup & Recover**. Check GUI SSH
fields, current local backup directory, and target confirmation. Select a
fixed/removable local disk, not a network path. Configure paths for the new
Windows account rather than copying the old username verbatim.

Optional: preserve `%LOCALAPPDATA%\VaultBackup\history.ini` privately as history.
Its old paths/SHA256 records do not prove files were transferred. Staging files
are not backups. Set a new backup password that you can recover independently
and create a genuine, owner-managed backup after migration.

## 9. Acceptance: verify the entire replacement

Keep the old server powered off. Inside the replacement VM:

```bash
systemctl is-enabled docker.service docker.socket containerd.service tailscaled.service
systemctl is-active docker.service docker.socket containerd.service tailscaled.service
sudo docker inspect --format '{{.State.Running}} {{.State.Paused}} {{.HostConfig.RestartPolicy.Name}}' bitwarden
sudo docker compose -f /opt/bitwarden/compose.yaml config --quiet
sudo tailscale serve status
sudo ufw status
sudo stat -c '%U:%G %a %n' /opt/bitwarden /opt/bitwarden/settings.env /opt/bitwarden/compose.yaml
sudo python3 - <<'PY'
import sqlite3
with sqlite3.connect('file:/opt/bitwarden/data/vault.db?mode=ro', uri=True) as db:
    result = db.execute('PRAGMA integrity_check').fetchone()[0]
    assert result == 'ok', 'SQLite integrity check failed'
print('SQLite integrity: ok')
PY
```

Expected: services enabled/active, container `true false always`, fixed
loopback-only port mapping, and SQLite `ok`. No unnecessary open firewall
ports, temporary proxy drop-ins, or publicly exposed vault endpoint.

From Windows, with normal TLS verification:

```powershell
curl.exe --noproxy '*' --connect-timeout 15 --max-time 30 -I $VaultUrl
$Response = curl.exe --noproxy '*' --connect-timeout 15 --max-time 30 -fsS "$VaultUrl/api/config"
if ($LASTEXITCODE -ne 0) { throw 'Private API request failed.' }
$Api = $Response | ConvertFrom-Json
if (-not $Api.version -or $Api.environment.vault -ne $VaultUrl) { throw 'Unexpected API origin or version.' }
```

Complete every acceptance item:

- [ ] HTTPS returns 200 and API origin equals the configured final origin.
- [ ] Browser fresh login and TOTP work; expected items and organizations exist.
- [ ] Existing attachments download correctly, if the backup contained any.
- [ ] Windows browser extension, iPhone, and macOS clients use the intended
  self-hosted server; a test edit syncs in both directions.
- [ ] Registration is disabled after owner enrollment and verified via API.
- [ ] Allowed Windows SSH succeeds; another owner device cannot access SSH;
  direct ports 80/8080 are denied by the saved policy.
- [ ] A genuine password-protected backup succeeds; its SHA256 is recorded;
  encrypted offsite upload and download verification are completed manually.
- [ ] A separate restore drill authenticates/decrypts that backup and validates
  the full restored service; a compression-only check is insufficient.
- [ ] Gracefully shut down the guest, confirm Powered off, then power it on.
  Without manual start commands, Docker/Bitwarden and private HTTPS/API become
  ready. Allow startup time; a transient 502 while Bitwarden initializes is not
  the final result. Record boot ID and service results.
- [ ] Tailscale key expiry, TLS renewal connectivity, and package maintenance
  have an owner and a recorded plan.

Do not use `sudo reboot` for this acceptance on the known affected Workstation
15.5.7/host combination. If VMware crashes, retain `vmware.log`, recover by
power-on only after it is off, and validate SQLite/service state. If the new PC
also crashes at cold boot, resolve VMware/host compatibility before accepting
the server; do not claim a completed rebuild.

## 10. Cutover, rollback, and daily operation

Only retire the old PC/VM after acceptance and a verified offsite backup. Keep
its powered-off image protected for rollback. If the new server fails before
new writes, shut it down before returning to the old server. If it accepted new
writes, returning to old data loses those writes; preserve a fresh backup and
plan reconciliation. Never run two instances with the same copied Tailscale
identity or let two diverging vault databases serve the same origin.

Daily: power on the VM when required; Docker and Bitwarden start automatically.
Create manual backups and upload only encrypted `.vbk` files. Keep independent
password/recovery material and multiple recovery points. Before maintenance,
back up and verify. `restart: always` means a manually stopped container starts
again when Docker restarts; maintenance that must persist across boot requires
an explicit temporary restart-policy/Compose change and later restoration.

Troubleshooting: check VM power, LAN SSH, Tailscale policy/IP, Docker status,
container running/paused state, Serve mapping, and HTTPS/API in that order.
For failed recovery, inspect the root-only
`/var/backups/bitwarden/recover-*/status.json` and resolve incomplete rollback
before retrying. Do not delete journals/data or recreate containers blindly.
For backup staging cleanup, identify the specific failed operation and confirm
no backup/recovery process is active before removing its plaintext files.

Further tool details: [Backup & Recover README](../tools/vault-backup/README.md).
This runbook does not depend on the private historical operations log being
available on the new PC.
