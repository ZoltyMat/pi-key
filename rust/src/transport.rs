use anyhow::{bail, Context, Result};
use log::{debug, info, warn};
use std::path::PathBuf;
use tokio::sync::Mutex;

use crate::keymap;

// ── Transport trait ──────────────────────────────────────────────────────────

/// Abstraction over BT and USB gadget HID transports.
#[allow(async_fn_in_trait)]
pub trait HidTransport: Send + Sync {
    async fn connect(&mut self) -> Result<()>;
    async fn disconnect(&mut self) -> Result<()>;
    async fn send_keyboard_report(&self, report: &[u8]) -> Result<()>;
    async fn send_mouse_report(&self, report: &[u8]) -> Result<()>;
    fn is_connected(&self) -> bool;
}

// ── Bluetooth transport (L2CAP sockets) ──────────────────────────────────────

/// L2CAP PSMs for HID
const PSM_CTRL: u16 = 0x0011;
const PSM_INTR: u16 = 0x0013;

/// HID interrupt header byte (DATA | INPUT)
const HID_INTR_HEADER: u8 = 0xA1;

/// HID descriptor — combo keyboard + mouse (boot-compatible).
/// Matches the Python implementation byte-for-byte.
pub const HID_DESCRIPTOR: &[u8] = &[
    // Keyboard
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15,
    0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x03,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81,
    0x00, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02, 0x95, 0x01,
    0x75, 0x03, 0x91, 0x03, 0xC0, // Mouse
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19,
    0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x05, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25,
    0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
];

/// SDP record template. The `{HID_DESC_HEX}` placeholder is replaced at runtime.
fn sdp_record() -> String {
    let hid_hex = hex::encode(HID_DESCRIPTOR);
    format!(
        r#"<?xml version="1.0" encoding="UTF-8" ?>
<record>
  <attribute id="0x0001">
    <sequence><uuid value="0x1124"/></sequence>
  </attribute>
  <attribute id="0x0004">
    <sequence>
      <sequence><uuid value="0x0100"/><uint16 value="0x0011"/></sequence>
      <sequence><uuid value="0x0011"/></sequence>
    </sequence>
  </attribute>
  <attribute id="0x0005">
    <sequence><uuid value="0x1002"/></sequence>
  </attribute>
  <attribute id="0x0006">
    <sequence>
      <uint16 value="0x656e"/><uint16 value="0x006a"/>
      <uint16 value="0x0100"/>
    </sequence>
  </attribute>
  <attribute id="0x0009">
    <sequence>
      <sequence><uuid value="0x1124"/><uint16 value="0x0100"/></sequence>
    </sequence>
  </attribute>
  <attribute id="0x000d">
    <sequence>
      <sequence>
        <sequence><uuid value="0x0100"/><uint16 value="0x0013"/></sequence>
        <sequence><uuid value="0x0011"/></sequence>
      </sequence>
    </sequence>
  </attribute>
  <attribute id="0x0100">
    <text value="Logitech K380 Multi-Device Keyboard"/>
  </attribute>
  <attribute id="0x0101">
    <text value="Logitech"/>
  </attribute>
  <attribute id="0x0102">
    <text value="K380"/>
  </attribute>
  <attribute id="0x0200"><uint16 value="0x0100"/></attribute>
  <attribute id="0x0201"><uint16 value="0x0111"/></attribute>
  <attribute id="0x0202"><uint8  value="0x40"/></attribute>
  <attribute id="0x0203"><uint8  value="0x00"/></attribute>
  <attribute id="0x0204"><boolean value="false"/></attribute>
  <attribute id="0x0205"><boolean value="false"/></attribute>
  <attribute id="0x0206">
    <sequence>
      <sequence>
        <uint8 value="0x22"/>
        <text encoding="hex" value="{hid_hex}"/>
      </sequence>
    </sequence>
  </attribute>
  <attribute id="0x0207">
    <sequence>
      <sequence><uint16 value="0x0409"/><uint16 value="0x0100"/></sequence>
    </sequence>
  </attribute>
  <attribute id="0x020b"><uint16 value="0x0100"/></attribute>
  <attribute id="0x020c"><uint16 value="0x0c80"/></attribute>
  <attribute id="0x020d"><boolean value="false"/></attribute>
  <attribute id="0x020e"><boolean value="true"/></attribute>
  <attribute id="0x020f"><uint16 value="0x0640"/></attribute>
  <attribute id="0x0210"><uint16 value="0x0320"/></attribute>
</record>"#
    )
}

pub struct BluetoothTransport {
    target_mac: String,
    #[cfg(target_os = "linux")]
    intr_fd: Mutex<Option<std::os::unix::io::RawFd>>,
    #[cfg(target_os = "linux")]
    ctrl_fd: Mutex<Option<std::os::unix::io::RawFd>>,
    connected: std::sync::atomic::AtomicBool,
}

impl BluetoothTransport {
    pub fn new(target_mac: String) -> Self {
        Self {
            target_mac,
            #[cfg(target_os = "linux")]
            intr_fd: Mutex::new(None),
            #[cfg(target_os = "linux")]
            ctrl_fd: Mutex::new(None),
            connected: std::sync::atomic::AtomicBool::new(false),
        }
    }

    /// Register the HID SDP profile via bluetoothctl subprocess.
    /// On a real Pi you'd use D-Bus directly; we shell out for simplicity.
    #[cfg(target_os = "linux")]
    async fn setup_sdp(&self) -> Result<()> {
        use tokio::process::Command;

        // Write SDP record to a temp file and register via sdptool
        let sdp = sdp_record();
        let tmp = "/tmp/pikey_sdp.xml";
        tokio::fs::write(tmp, &sdp).await?;

        let status = Command::new("sdptool")
            .args(["add", "--handle=0x10000", "--channel=1", "SP"])
            .status()
            .await
            .context("Failed to run sdptool")?;
        if !status.success() {
            warn!("sdptool returned non-zero (may be OK if profile already registered)");
        }

        // Make discoverable + pairable
        let _ = Command::new("bluetoothctl")
            .args(["discoverable", "on"])
            .status()
            .await;
        let _ = Command::new("bluetoothctl")
            .args(["pairable", "on"])
            .status()
            .await;

        info!("Bluetooth SDP profile registered, device discoverable");
        Ok(())
    }

    #[cfg(not(target_os = "linux"))]
    async fn setup_sdp(&self) -> Result<()> {
        warn!("Bluetooth SDP setup is only supported on Linux — skipping");
        Ok(())
    }
}

#[cfg(target_os = "linux")]
mod bt_linux {
    use super::*;
    use std::mem;
    use std::os::unix::io::RawFd;

    // BlueZ L2CAP socket constants
    const AF_BLUETOOTH: libc::c_int = 31;
    const BTPROTO_L2CAP: libc::c_int = 0;

    #[repr(C)]
    struct SockaddrL2 {
        l2_family: libc::sa_family_t,
        l2_psm: u16,
        l2_bdaddr: [u8; 6],
        l2_cid: u16,
        l2_bdaddr_type: u8,
    }

    fn parse_mac(mac: &str) -> Result<[u8; 6]> {
        let parts: Vec<&str> = mac.split(':').collect();
        if parts.len() != 6 {
            bail!("Invalid MAC address: {}", mac);
        }
        let mut addr = [0u8; 6];
        for (i, part) in parts.iter().enumerate() {
            // BlueZ uses reversed byte order
            addr[5 - i] = u8::from_str_radix(part, 16)
                .with_context(|| format!("Invalid MAC octet: {}", part))?;
        }
        Ok(addr)
    }

    fn l2cap_socket() -> Result<RawFd> {
        let fd = unsafe { libc::socket(AF_BLUETOOTH, libc::SOCK_SEQPACKET, BTPROTO_L2CAP) };
        if fd < 0 {
            bail!(
                "Failed to create L2CAP socket: {}",
                std::io::Error::last_os_error()
            );
        }
        Ok(fd)
    }

    fn l2cap_bind(fd: RawFd, psm: u16) -> Result<()> {
        let addr = SockaddrL2 {
            l2_family: AF_BLUETOOTH as u16,
            l2_psm: psm.to_le(),
            l2_bdaddr: [0u8; 6], // BDADDR_ANY
            l2_cid: 0,
            l2_bdaddr_type: 0,
        };
        let ret = unsafe {
            libc::bind(
                fd,
                &addr as *const SockaddrL2 as *const libc::sockaddr,
                mem::size_of::<SockaddrL2>() as libc::socklen_t,
            )
        };
        if ret < 0 {
            bail!(
                "Failed to bind L2CAP PSM {:#06x}: {}",
                psm,
                std::io::Error::last_os_error()
            );
        }
        Ok(())
    }

    fn l2cap_listen(fd: RawFd) -> Result<()> {
        let ret = unsafe { libc::listen(fd, 1) };
        if ret < 0 {
            bail!(
                "Failed to listen on L2CAP: {}",
                std::io::Error::last_os_error()
            );
        }
        Ok(())
    }

    fn l2cap_accept(fd: RawFd) -> Result<RawFd> {
        let client = unsafe { libc::accept(fd, std::ptr::null_mut(), std::ptr::null_mut()) };
        if client < 0 {
            bail!(
                "Failed to accept L2CAP connection: {}",
                std::io::Error::last_os_error()
            );
        }
        Ok(client)
    }

    fn l2cap_connect(fd: RawFd, mac: &[u8; 6], psm: u16) -> Result<()> {
        let addr = SockaddrL2 {
            l2_family: AF_BLUETOOTH as u16,
            l2_psm: psm.to_le(),
            l2_bdaddr: *mac,
            l2_cid: 0,
            l2_bdaddr_type: 0,
        };
        let ret = unsafe {
            libc::connect(
                fd,
                &addr as *const SockaddrL2 as *const libc::sockaddr,
                mem::size_of::<SockaddrL2>() as libc::socklen_t,
            )
        };
        if ret < 0 {
            bail!(
                "Failed to connect L2CAP: {}",
                std::io::Error::last_os_error()
            );
        }
        Ok(())
    }

    fn send_fd(fd: RawFd, data: &[u8]) -> Result<()> {
        let ret =
            unsafe { libc::send(fd, data.as_ptr() as *const libc::c_void, data.len(), 0) };
        if ret < 0 {
            bail!(
                "L2CAP send failed: {}",
                std::io::Error::last_os_error()
            );
        }
        Ok(())
    }

    impl HidTransport for BluetoothTransport {
        async fn connect(&mut self) -> Result<()> {
            self.setup_sdp().await?;

            if self.target_mac.is_empty() {
                // Listen mode — wait for incoming connections
                info!("Waiting for incoming HID connection...");

                let ctrl_server = l2cap_socket()?;
                l2cap_bind(ctrl_server, PSM_CTRL)?;
                l2cap_listen(ctrl_server)?;

                let intr_server = l2cap_socket()?;
                l2cap_bind(intr_server, PSM_INTR)?;
                l2cap_listen(intr_server)?;

                let ctrl_client = tokio::task::spawn_blocking(move || l2cap_accept(ctrl_server))
                    .await??;
                info!("Control channel connected");

                let intr_client = tokio::task::spawn_blocking(move || l2cap_accept(intr_server))
                    .await??;
                info!("Interrupt channel connected");

                *self.ctrl_fd.lock().await = Some(ctrl_client);
                *self.intr_fd.lock().await = Some(intr_client);

                // Close server sockets
                unsafe {
                    libc::close(ctrl_server);
                    libc::close(intr_server);
                }
            } else {
                // Active connect to known host
                let mac = parse_mac(&self.target_mac)?;
                info!("Connecting to {}...", self.target_mac);

                let ctrl = l2cap_socket()?;
                l2cap_connect(ctrl, &mac, PSM_CTRL)?;
                info!("Control channel connected");

                let intr = l2cap_socket()?;
                l2cap_connect(intr, &mac, PSM_INTR)?;
                info!("Interrupt channel connected");

                *self.ctrl_fd.lock().await = Some(ctrl);
                *self.intr_fd.lock().await = Some(intr);
            }

            self.connected
                .store(true, std::sync::atomic::Ordering::SeqCst);
            info!("HID device fully connected");
            Ok(())
        }

        async fn disconnect(&mut self) -> Result<()> {
            if let Some(fd) = self.ctrl_fd.lock().await.take() {
                unsafe { libc::close(fd) };
            }
            if let Some(fd) = self.intr_fd.lock().await.take() {
                unsafe { libc::close(fd) };
            }
            self.connected
                .store(false, std::sync::atomic::Ordering::SeqCst);
            info!("Bluetooth disconnected");
            Ok(())
        }

        async fn send_keyboard_report(&self, report: &[u8]) -> Result<()> {
            let guard = self.intr_fd.lock().await;
            let fd = guard.as_ref().context("Not connected")?;
            let mut buf = vec![HID_INTR_HEADER];
            buf.extend_from_slice(report);
            send_fd(*fd, &buf)
        }

        async fn send_mouse_report(&self, report: &[u8]) -> Result<()> {
            let guard = self.intr_fd.lock().await;
            let fd = guard.as_ref().context("Not connected")?;
            let mut buf = vec![HID_INTR_HEADER];
            buf.extend_from_slice(report);
            send_fd(*fd, &buf)
        }

        fn is_connected(&self) -> bool {
            self.connected
                .load(std::sync::atomic::Ordering::SeqCst)
        }
    }
}

// Non-Linux stub for BluetoothTransport so it compiles on macOS
#[cfg(not(target_os = "linux"))]
impl HidTransport for BluetoothTransport {
    async fn connect(&mut self) -> Result<()> {
        self.setup_sdp().await?;
        bail!("Bluetooth L2CAP transport requires Linux with BlueZ")
    }
    async fn disconnect(&mut self) -> Result<()> {
        Ok(())
    }
    async fn send_keyboard_report(&self, _report: &[u8]) -> Result<()> {
        bail!("Bluetooth not available on this platform")
    }
    async fn send_mouse_report(&self, _report: &[u8]) -> Result<()> {
        bail!("Bluetooth not available on this platform")
    }
    fn is_connected(&self) -> bool {
        false
    }
}

// ── USB Gadget transport ─────────────────────────────────────────────────────

pub struct UsbGadgetTransport {
    gadget_name: String,
    #[cfg(target_os = "linux")]
    kbd_file: Mutex<Option<tokio::fs::File>>,
    #[cfg(target_os = "linux")]
    mouse_file: Mutex<Option<tokio::fs::File>>,
    connected: std::sync::atomic::AtomicBool,
}

impl UsbGadgetTransport {
    pub fn new() -> Self {
        Self {
            gadget_name: "pikey".to_string(),
            #[cfg(target_os = "linux")]
            kbd_file: Mutex::new(None),
            #[cfg(target_os = "linux")]
            mouse_file: Mutex::new(None),
            connected: std::sync::atomic::AtomicBool::new(false),
        }
    }

    /// Set up USB gadget via ConfigFS.
    #[cfg(target_os = "linux")]
    async fn setup_configfs(&self) -> Result<()> {
        use tokio::fs;
        use tokio::process::Command;

        let base = format!("/sys/kernel/config/usb_gadget/{}", self.gadget_name);

        // Load dwc2 module
        let _ = Command::new("modprobe").arg("dwc2").status().await;
        let _ = Command::new("modprobe").arg("libcomposite").status().await;

        // Create gadget directory
        fs::create_dir_all(&base).await.ok();

        // Set IDs (Logitech vendor ID, K380 product ID)
        fs::write(format!("{}/idVendor", base), "0x046d").await?;
        fs::write(format!("{}/idProduct", base), "0xb342").await?;
        fs::write(format!("{}/bcdDevice", base), "0x0100").await?;
        fs::write(format!("{}/bcdUSB", base), "0x0200").await?;
        fs::write(format!("{}/bDeviceClass", base), "0x00").await?;
        fs::write(format!("{}/bDeviceSubClass", base), "0x00").await?;
        fs::write(format!("{}/bDeviceProtocol", base), "0x00").await?;

        // Strings
        let strings = format!("{}/strings/0x409", base);
        fs::create_dir_all(&strings).await.ok();
        fs::write(format!("{}/serialnumber", strings), "000000001").await?;
        fs::write(format!("{}/manufacturer", strings), "Logitech").await?;
        fs::write(
            format!("{}/product", strings),
            "K380 Multi-Device Keyboard",
        )
        .await?;

        // Keyboard function (HID)
        let kbd_func = format!("{}/functions/hid.keyboard", base);
        fs::create_dir_all(&kbd_func).await.ok();
        fs::write(format!("{}/protocol", kbd_func), "1").await?; // keyboard
        fs::write(format!("{}/subclass", kbd_func), "1").await?; // boot interface
        fs::write(format!("{}/report_length", kbd_func), "9").await?;
        fs::write(format!("{}/report_desc", kbd_func), &HID_DESCRIPTOR[..63]).await?;

        // Mouse function (HID)
        let mouse_func = format!("{}/functions/hid.mouse", base);
        fs::create_dir_all(&mouse_func).await.ok();
        fs::write(format!("{}/protocol", mouse_func), "2").await?; // mouse
        fs::write(format!("{}/subclass", mouse_func), "1").await?;
        fs::write(format!("{}/report_length", mouse_func), "5").await?;
        fs::write(format!("{}/report_desc", mouse_func), &HID_DESCRIPTOR[63..]).await?;

        // Configuration
        let config = format!("{}/configs/c.1", base);
        fs::create_dir_all(format!("{}/strings/0x409", config))
            .await
            .ok();
        fs::write(format!("{}/strings/0x409/configuration", config), "PiKey HID").await?;
        fs::write(format!("{}/MaxPower", config), "100").await?;

        // Symlink functions into config
        let _ = tokio::fs::symlink(
            &kbd_func,
            format!("{}/hid.keyboard", config),
        )
        .await;
        let _ = tokio::fs::symlink(
            &mouse_func,
            format!("{}/hid.mouse", config),
        )
        .await;

        // Bind to UDC
        let udc_dir = "/sys/class/udc";
        let mut entries = fs::read_dir(udc_dir).await?;
        if let Some(entry) = entries.next_entry().await? {
            let udc_name = entry.file_name().to_string_lossy().to_string();
            fs::write(format!("{}/UDC", base), &udc_name).await?;
            info!("USB gadget bound to UDC: {}", udc_name);
        } else {
            bail!("No UDC found in /sys/class/udc — is dwc2 overlay enabled?");
        }

        Ok(())
    }

    #[cfg(not(target_os = "linux"))]
    async fn setup_configfs(&self) -> Result<()> {
        warn!("USB gadget ConfigFS setup only supported on Linux — skipping");
        Ok(())
    }
}

#[cfg(target_os = "linux")]
impl HidTransport for UsbGadgetTransport {
    async fn connect(&mut self) -> Result<()> {
        self.setup_configfs().await?;

        // Open /dev/hidg0 (keyboard) and /dev/hidg1 (mouse)
        let kbd = tokio::fs::OpenOptions::new()
            .write(true)
            .open("/dev/hidg0")
            .await
            .context("Failed to open /dev/hidg0 — is USB gadget configured?")?;
        let mouse = tokio::fs::OpenOptions::new()
            .write(true)
            .open("/dev/hidg1")
            .await
            .context("Failed to open /dev/hidg1 — is USB gadget configured?")?;

        *self.kbd_file.lock().await = Some(kbd);
        *self.mouse_file.lock().await = Some(mouse);
        self.connected
            .store(true, std::sync::atomic::Ordering::SeqCst);
        info!("USB gadget HID connected via /dev/hidg{{0,1}}");
        Ok(())
    }

    async fn disconnect(&mut self) -> Result<()> {
        self.kbd_file.lock().await.take();
        self.mouse_file.lock().await.take();
        self.connected
            .store(false, std::sync::atomic::Ordering::SeqCst);
        info!("USB gadget disconnected");
        Ok(())
    }

    async fn send_keyboard_report(&self, report: &[u8]) -> Result<()> {
        use tokio::io::AsyncWriteExt;
        let mut guard = self.kbd_file.lock().await;
        let f = guard.as_mut().context("USB gadget not connected")?;
        f.write_all(report).await?;
        f.flush().await?;
        Ok(())
    }

    async fn send_mouse_report(&self, report: &[u8]) -> Result<()> {
        use tokio::io::AsyncWriteExt;
        let mut guard = self.mouse_file.lock().await;
        let f = guard.as_mut().context("USB gadget not connected")?;
        f.write_all(report).await?;
        f.flush().await?;
        Ok(())
    }

    fn is_connected(&self) -> bool {
        self.connected
            .load(std::sync::atomic::Ordering::SeqCst)
    }
}

#[cfg(not(target_os = "linux"))]
impl HidTransport for UsbGadgetTransport {
    async fn connect(&mut self) -> Result<()> {
        self.setup_configfs().await?;
        bail!("USB gadget transport requires Linux with ConfigFS + dwc2")
    }
    async fn disconnect(&mut self) -> Result<()> {
        Ok(())
    }
    async fn send_keyboard_report(&self, _report: &[u8]) -> Result<()> {
        bail!("USB gadget not available on this platform")
    }
    async fn send_mouse_report(&self, _report: &[u8]) -> Result<()> {
        bail!("USB gadget not available on this platform")
    }
    fn is_connected(&self) -> bool {
        false
    }
}

// ── Convenience methods shared across transports ─────────────────────────────

/// Send a key press + release via any HidTransport.
pub async fn send_key<T: HidTransport>(transport: &T, modifier: u8, keycode: u8) -> Result<()> {
    let press = keymap::make_key_report(modifier, keycode);
    transport.send_keyboard_report(&press).await?;
    tokio::time::sleep(std::time::Duration::from_millis(8)).await;
    let release = keymap::key_release_report();
    transport.send_keyboard_report(&release).await?;
    Ok(())
}

/// Send a mouse movement via any HidTransport.
pub async fn send_mouse<T: HidTransport>(
    transport: &T,
    dx: i8,
    dy: i8,
    buttons: u8,
    wheel: i8,
) -> Result<()> {
    let report = keymap::mouse_report(buttons, dx, dy, wheel);
    transport.send_mouse_report(&report).await?;
    Ok(())
}

/// Type a single character via the transport.
pub async fn type_char<T: HidTransport>(transport: &T, ch: char) -> Result<()> {
    if let Some(report) = keymap::char_to_hid_report(ch) {
        let modifier = report[1];
        let keycode = report[3];
        send_key(transport, modifier, keycode).await?;
    } else {
        debug!("Unmapped character: {:?}", ch);
    }
    Ok(())
}

/// Type a backspace via the transport.
pub async fn type_backspace<T: HidTransport>(transport: &T) -> Result<()> {
    send_key(transport, keymap::BACKSPACE.modifier, keymap::BACKSPACE.keycode).await
}

/// Auto-detect best available transport.
pub fn auto_detect_transport(target_mac: &str) -> Box<dyn HidTransport> {
    // Check if USB gadget device exists
    if PathBuf::from("/dev/hidg0").exists() {
        info!("Auto-detected USB gadget transport (/dev/hidg0 exists)");
        Box::new(UsbGadgetTransport::new())
    } else {
        info!("Using Bluetooth transport");
        Box::new(BluetoothTransport::new(target_mac.to_string()))
    }
}
