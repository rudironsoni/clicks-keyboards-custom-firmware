import Foundation

/// The expected factory keymap decoded from the stock V122 image
/// (stock 0x2b1c..0x3010; see keyboards/ck5200/docs/STOCK_DECODE.md).
/// The self-test compares the connected firmware's keymap against this.
///
/// Crossing = (matrix row, PA bit). The custom firmware uses the same
/// scan indexing as stock: PB row high, read PA0..PA5.
enum FactoryLayout {
    /// Layer 0: base QWERTY. One key per electrical crossing; (2,3) is
    /// the stock spare and stays KC_NO.
    static let layer0: [UInt16] = [
        // row 0:  q       u       k       Bksp    Enter   Cons-A
        0x0004, 0x0018, 0x000e, 0x002a, 0x0028, 0x5da1,
        // row 1:  w       i       d       l       v       Cons-B
        0x001a, 0x000c, 0x0007, 0x000f, 0x0019, 0x5da2,
        // row 2:  e       o       f       (spare) b       Cmd
        0x0008, 0x0012, 0x0009, 0x0000, 0x0005, 0xe3,
        // row 3:  r       p       g       z       n       Space
        0x0015, 0x0013, 0x000a, 0x001d, 0x0011, 0x002c,
        // row 4:  t       a       h       x       m       Ctrl
        0x0017, 0x0004, 0x000b, 0x001b, 0x0010, 0xe0,
        // row 5:  y       s       LT(2,J) c       OSL(1)  iOS-KB
        0x001c, 0x0016, 0x420d, 0x0006, 0x5281, 0x5da3,
    ]

    /// Layer 1: SYM (the stock alternate values).
    static let layer1: [UInt16] = [
        // row 0:  1       7       '       tr      tr      tr
        0x001e, 0x0024, 0x0034, 0x0001, 0x0001, 0x0001,
        // row 1:  2       8       ;       '       /       tr
        0x001f, 0x0025, 0x0033, 0x0034, 0x0038, 0x0001,
        // row 2:  3       9       ;       NO      1       tr
        0x0020, 0x0026, 0x0033, 0x0000, 0x001e, 0x0001,
        // row 3:  4       0       9       tr      ,       tr
        0x0021, 0x0027, 0x0026, 0x0001, 0x0036, 0x0001,
        // row 4:  5       -       0       7       .       tr
        0x0022, 0x002d, 0x0027, 0x0024, 0x0037, 0x0001,
        // row 5:  6       /       tr      2       tr      tr
        0x0023, 0x0038, 0x0001, 0x001f, 0x0001, 0x0001,
    ]

    /// Layer 2: cursor (the stock cursor-mode arrows, on hold).
    static let layer2: [UInt16] = [
        // row 0:  tr      tr      Down    tr      tr      tr
        0x0001, 0x0001, 0x0051, 0x0001, 0x0001, 0x0001,
        // row 1:  Up      Up      Right   Right   tr      tr
        0x0052, 0x0052, 0x004f, 0x004f, 0x0001, 0x0001,
        // row 2:  tr      tr      tr      NO      tr      tr
        0x0001, 0x0001, 0x0001, 0x0000, 0x0001, 0x0001,
        // row 3:  tr      tr      tr      tr      tr      tr
        0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
        // row 4:  tr      Left    tr      tr      tr      tr
        0x0001, 0x0050, 0x0001, 0x0001, 0x0001, 0x0001,
        // row 5:  tr      Down    tr      tr      tr      tr
        0x0001, 0x0051, 0x0001, 0x0001, 0x0001, 0x0001,
    ]

    /// Layer 3: reserved (all transparent).
    static let layer3: [UInt16] = Array(repeating: 0x0001, count: 36)

    /// All four layers in order.
    static let allLayers: [[UInt16]] = [layer0, layer1, layer2, layer3]

    /// The keymap in the controller's [layer][row][column] shape.
    static var asRowMajor: [[[UInt16]]] {
        allLayers.map { layer in
            (0..<6).map { row in
                Array(layer[(row * 6)..<(row * 6 + 6)])
            }
        }
    }

    /// QMK encodings for the three special keycodes on layer 0:
    /// LT(2, KC_J) = layer 2, key 0x0a ('j') = 0x4000 | (2 << 8) | 0x0a.
    /// OSL(1) = 0x5280 | 1.
    static let lt2J: UInt16 = 0x420d
    static let osl1: UInt16 = 0x5281
}
