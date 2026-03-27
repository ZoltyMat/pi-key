use std::collections::HashMap;
use std::sync::LazyLock;

/// USB HID keycode entry: (modifier_byte, keycode)
#[derive(Debug, Clone, Copy)]
pub struct HidKey {
    pub modifier: u8,
    pub keycode: u8,
}

pub const BACKSPACE: HidKey = HidKey {
    modifier: 0x00,
    keycode: 0x2A,
};

/// Build an 8-byte keyboard report (report_id=0x01).
/// Format: [report_id, modifier, 0x00, keycode, 0, 0, 0, 0, 0]
/// We return 9 bytes (including report ID) to match the Python impl.
pub fn char_to_hid_report(c: char) -> Option<[u8; 9]> {
    let key = KEYMAP.get(&c)?;
    Some(make_key_report(key.modifier, key.keycode))
}

pub fn make_key_report(modifier: u8, keycode: u8) -> [u8; 9] {
    [0x01, modifier, 0x00, keycode, 0, 0, 0, 0, 0]
}

/// All-zeros key release report (report_id=0x01).
pub fn key_release_report() -> [u8; 9] {
    [0x01, 0, 0, 0, 0, 0, 0, 0, 0]
}

/// Build a 5-byte mouse report (report_id=0x02).
/// Format: [report_id, buttons, dx, dy, wheel]
pub fn mouse_report(buttons: u8, dx: i8, dy: i8, wheel: i8) -> [u8; 5] {
    [0x02, buttons, dx as u8, dy as u8, wheel as u8]
}

/// Nearby keys on QWERTY for typo simulation.
pub static NEARBY_KEYS: LazyLock<HashMap<char, &'static str>> = LazyLock::new(|| {
    let mut m = HashMap::new();
    m.insert('a', "sqwz");
    m.insert('b', "vghn");
    m.insert('c', "xdfv");
    m.insert('d', "sfgxce");
    m.insert('e', "wsdr");
    m.insert('f', "dgrtvc");
    m.insert('g', "fhtybv");
    m.insert('h', "gjyun");
    m.insert('i', "uojk");
    m.insert('j', "hkuim");
    m.insert('k', "jloi");
    m.insert('l', "kop");
    m.insert('m', "njk");
    m.insert('n', "bhmj");
    m.insert('o', "iklp");
    m.insert('p', "ol");
    m.insert('q', "wa");
    m.insert('r', "etfd");
    m.insert('s', "adwxze");
    m.insert('t', "ryfg");
    m.insert('u', "yhji");
    m.insert('v', "cfgb");
    m.insert('w', "qase");
    m.insert('x', "zsdc");
    m.insert('y', "tghu");
    m.insert('z', "asx");
    m
});

/// Full USB HID keycode map matching the Python implementation.
static KEYMAP: LazyLock<HashMap<char, HidKey>> = LazyLock::new(|| {
    let mut m = HashMap::new();
    let k = |modifier: u8, keycode: u8| HidKey { modifier, keycode };

    // Whitespace
    m.insert(' ', k(0x00, 0x2C));
    m.insert('\n', k(0x00, 0x28));
    m.insert('\t', k(0x00, 0x2B));

    // Lowercase
    m.insert('a', k(0x00, 0x04));
    m.insert('b', k(0x00, 0x05));
    m.insert('c', k(0x00, 0x06));
    m.insert('d', k(0x00, 0x07));
    m.insert('e', k(0x00, 0x08));
    m.insert('f', k(0x00, 0x09));
    m.insert('g', k(0x00, 0x0A));
    m.insert('h', k(0x00, 0x0B));
    m.insert('i', k(0x00, 0x0C));
    m.insert('j', k(0x00, 0x0D));
    m.insert('k', k(0x00, 0x0E));
    m.insert('l', k(0x00, 0x0F));
    m.insert('m', k(0x00, 0x10));
    m.insert('n', k(0x00, 0x11));
    m.insert('o', k(0x00, 0x12));
    m.insert('p', k(0x00, 0x13));
    m.insert('q', k(0x00, 0x14));
    m.insert('r', k(0x00, 0x15));
    m.insert('s', k(0x00, 0x16));
    m.insert('t', k(0x00, 0x17));
    m.insert('u', k(0x00, 0x18));
    m.insert('v', k(0x00, 0x19));
    m.insert('w', k(0x00, 0x1A));
    m.insert('x', k(0x00, 0x1B));
    m.insert('y', k(0x00, 0x1C));
    m.insert('z', k(0x00, 0x1D));

    // Uppercase (shift = 0x02)
    m.insert('A', k(0x02, 0x04));
    m.insert('B', k(0x02, 0x05));
    m.insert('C', k(0x02, 0x06));
    m.insert('D', k(0x02, 0x07));
    m.insert('E', k(0x02, 0x08));
    m.insert('F', k(0x02, 0x09));
    m.insert('G', k(0x02, 0x0A));
    m.insert('H', k(0x02, 0x0B));
    m.insert('I', k(0x02, 0x0C));
    m.insert('J', k(0x02, 0x0D));
    m.insert('K', k(0x02, 0x0E));
    m.insert('L', k(0x02, 0x0F));
    m.insert('M', k(0x02, 0x10));
    m.insert('N', k(0x02, 0x11));
    m.insert('O', k(0x02, 0x12));
    m.insert('P', k(0x02, 0x13));
    m.insert('Q', k(0x02, 0x14));
    m.insert('R', k(0x02, 0x15));
    m.insert('S', k(0x02, 0x16));
    m.insert('T', k(0x02, 0x17));
    m.insert('U', k(0x02, 0x18));
    m.insert('V', k(0x02, 0x19));
    m.insert('W', k(0x02, 0x1A));
    m.insert('X', k(0x02, 0x1B));
    m.insert('Y', k(0x02, 0x1C));
    m.insert('Z', k(0x02, 0x1D));

    // Numbers
    m.insert('1', k(0x00, 0x1E));
    m.insert('2', k(0x00, 0x1F));
    m.insert('3', k(0x00, 0x20));
    m.insert('4', k(0x00, 0x21));
    m.insert('5', k(0x00, 0x22));
    m.insert('6', k(0x00, 0x23));
    m.insert('7', k(0x00, 0x24));
    m.insert('8', k(0x00, 0x25));
    m.insert('9', k(0x00, 0x26));
    m.insert('0', k(0x00, 0x27));

    // Shifted symbols
    m.insert('!', k(0x02, 0x1E));
    m.insert('@', k(0x02, 0x1F));
    m.insert('#', k(0x02, 0x20));
    m.insert('$', k(0x02, 0x21));
    m.insert('%', k(0x02, 0x22));
    m.insert('^', k(0x02, 0x23));
    m.insert('&', k(0x02, 0x24));
    m.insert('*', k(0x02, 0x25));
    m.insert('(', k(0x02, 0x26));
    m.insert(')', k(0x02, 0x27));

    // Punctuation
    m.insert('-', k(0x00, 0x2D));
    m.insert('_', k(0x02, 0x2D));
    m.insert('=', k(0x00, 0x2E));
    m.insert('+', k(0x02, 0x2E));
    m.insert('[', k(0x00, 0x2F));
    m.insert('{', k(0x02, 0x2F));
    m.insert(']', k(0x00, 0x30));
    m.insert('}', k(0x02, 0x30));
    m.insert('\\', k(0x00, 0x31));
    m.insert('|', k(0x02, 0x31));
    m.insert(';', k(0x00, 0x33));
    m.insert(':', k(0x02, 0x33));
    m.insert('\'', k(0x00, 0x34));
    m.insert('"', k(0x02, 0x34));
    m.insert('`', k(0x00, 0x35));
    m.insert('~', k(0x02, 0x35));
    m.insert(',', k(0x00, 0x36));
    m.insert('<', k(0x02, 0x36));
    m.insert('.', k(0x00, 0x37));
    m.insert('>', k(0x02, 0x37));
    m.insert('/', k(0x00, 0x38));
    m.insert('?', k(0x02, 0x38));

    m
});
