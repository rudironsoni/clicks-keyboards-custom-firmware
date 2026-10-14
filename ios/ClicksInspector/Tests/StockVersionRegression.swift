import Foundation

@main
struct StockVersionRegression {
    static func main() throws {
        var parser = StockReadAccumulator(command: .version)
        let mismatch: [UInt8] = [0x08, 0x02, 0x03, 0x00, 0x01, 0x20, 0x01, 0x23]
        var rejected = false
        do { _ = try parser.append(mismatch) } catch { rejected = true }
        precondition(rejected, "mismatched version was validated")
        precondition(parser.bytes == mismatch, "retain actual failed reply")
        print("PASS mismatched version rejected")
    }
}
