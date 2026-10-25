import Foundation

final class BootReaderLink: KeyboardPacketLink {
    var onFragment: (([UInt8]) -> Void)?
    var onClosed: ((String) -> Void)?
    private let process = Process()
    private let input = Pipe()
    private let output = Pipe()
    private var closed = false
    private(set) var requests: [[UInt8]] = []

    init(runner: String, image: String, trace: String) throws {
        process.executableURL = URL(fileURLWithPath: runner)
        process.arguments = [image, trace, "bootloader-server"]
        process.standardInput = input
        process.standardOutput = output
        process.environment = ProcessInfo.processInfo.environment.merging(["UBSAN_OPTIONS":"halt_on_error=1"]) { _, value in value }
        try process.run()
        precondition(readLine() == "ready")
    }

    private func readLine() -> String {
        var bytes = [UInt8]()
        while let data = try? output.fileHandleForReading.read(upToCount: 1), let byte = data.first {
            if byte == 10 { break }
            bytes.append(byte)
        }
        return String(decoding: bytes, as: UTF8.self)
    }

    func send(_ bytes: [UInt8]) {
        precondition(!closed)
        requests.append(bytes)
        input.fileHandleForWriting.write(Data(("p " + bytes.map { String(format:"%02x",$0) }.joined() + "\n").utf8))
        let line = readLine()
        precondition(line.hasPrefix("R "), line)
        let hex = Array(line.dropFirst(2))
        let reply = stride(from:0,to:hex.count,by:2).map { UInt8(String(hex[$0...$0+1]),radix:16)! }
        onFragment?(Array(reply.prefix(3)))
        onFragment?(Array(reply.dropFirst(3)))
    }

    func close() {
        guard !closed else { return }
        closed = true
        input.fileHandleForWriting.write(Data("q\n".utf8))
        process.waitUntilExit()
        precondition(process.terminationStatus == 0)
    }
}

@main
struct BootloaderDumpTests {
    @MainActor
    static func main() async throws {
        let args = CommandLine.arguments
        precondition(args.count == 5, "runner patched-image stock-image output-directory")
        let output = URL(fileURLWithPath: args[4], isDirectory: true)
        try FileManager.default.createDirectory(at: output,withIntermediateDirectories:true)
        let target = CustomFirmwareController.ConnectedTarget(model:"CK-5200",firmwareRevision:"1.2.2",hardwareRevision:"1.2.0")
        let readerImage = [UInt8](try Data(contentsOf:URL(fileURLWithPath:args[2])))
        let prepared = try PreparedFirmwareImage(readerImage, artifact:.bootloaderReader)
        precondition(prepared.bytes.count == 18648 && prepared.sha256 == CustomKeyboardImageAuthorization.authorizedBootloaderReaderSHA256)
        let controller = CustomFirmwareController()
        controller.flash(image:readerImage,rebootToInstall:true,artifact:.bootloaderReader)
        if case .failed = controller.phase {} else { fatalError("default build enabled reader installation") }
        let patched = try BootReaderLink(runner:args[1],image:args[2],trace:output.appendingPathComponent("reader.trace.jsonl").path)
        controller.connectedTarget = target
        controller.attach(patched)
        controller.identify()
        while controller.keymapOperationRunning { await Task.yield() }
        precondition(controller.lastVersionPayload == CustomKeyboardCommand.stockVersionPayload, String(describing:controller.phase))
        await controller.dumpBootloader(in:output)
        guard let url = controller.bootloaderDumpURL else { fatalError("reader failed: \(controller.phase)") }
        let expected = Data((0..<8192).map { UInt8(truncatingIfNeeded: ($0 * 73) ^ ($0 >> 5) ^ 0xA7) })
        let actual = try Data(contentsOf:url)
        precondition(actual == expected)
        precondition(patched.requests.filter { $0[1] == 8 }.count == 1024)
        precondition(patched.requests.allSatisfy { [3,8].contains($0[1]) })
        var rejected = [UInt8]()
        patched.onFragment = { rejected += $0 }
        patched.send([7,8,0x52,0x44,0x1F,0xF1,16])
        precondition(rejected == [4,2,8,1])
        rejected = []
        patched.send([2,0xA0])
        precondition(rejected == [4,2,0xA0,0], "shared stock reply tail must remain intact")
        patched.close()
        print("PASS boot extraction: production Swift client, stock receiver, compiled reader, two complete matching passes")

        let stockController = CustomFirmwareController()
        let stock = try BootReaderLink(runner:args[1],image:args[3],trace:output.appendingPathComponent("stock.trace.jsonl").path)
        stockController.connectedTarget = target
        stockController.attach(stock)
        stockController.identify()
        while stockController.keymapOperationRunning { await Task.yield() }
        await stockController.dumpBootloader(in:output.appendingPathComponent("must-not-export"))
        precondition(stockController.bootloaderDumpURL == nil)
        if case .failed = stockController.phase {} else { fatalError("stock rejection was not reported") }
        stock.close()
        print("PASS boot extraction failure: original stock rejected, no dump exported")
    }
}
