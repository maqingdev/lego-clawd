import AppKit
import Foundation

struct Pixel {
    var r: UInt8
    var g: UInt8
    var b: UInt8
    var a: UInt8
}

let args = CommandLine.arguments
guard args.count == 4 else {
    fputs("usage: prepare-macos-icons.swift MENU_PNG APP_PNG OUTPUT_RESOURCES_DIR\n", stderr)
    exit(2)
}

let menuURL = URL(fileURLWithPath: args[1])
let appURL = URL(fileURLWithPath: args[2])
let outputURL = URL(fileURLWithPath: args[3])
try FileManager.default.createDirectory(at: outputURL, withIntermediateDirectories: true)

func cgImage(from url: URL) -> CGImage {
    guard let image = NSImage(contentsOf: url),
          let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
        fatalError("could not read image: \(url.path)")
    }
    return cg
}

func pixels(from image: CGImage) -> [Pixel] {
    let width = image.width
    let height = image.height
    var data = [UInt8](repeating: 0, count: width * height * 4)
    let colorSpace = CGColorSpaceCreateDeviceRGB()
    guard let context = CGContext(
        data: &data,
        width: width,
        height: height,
        bitsPerComponent: 8,
        bytesPerRow: width * 4,
        space: colorSpace,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
    ) else {
        fatalError("could not create bitmap context")
    }
    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    return stride(from: 0, to: data.count, by: 4).map {
        Pixel(r: data[$0], g: data[$0 + 1], b: data[$0 + 2], a: data[$0 + 3])
    }
}

func foregroundBounds(_ pixels: [Pixel], width: Int, height: Int, threshold: Int) -> CGRect {
    var minX = width
    var minY = height
    var maxX = 0
    var maxY = 0

    for y in 0..<height {
        for x in 0..<width {
            let p = pixels[y * width + x]
            let lum = (Int(p.r) + Int(p.g) + Int(p.b)) / 3
            if p.a > 8 && lum < threshold {
                minX = min(minX, x)
                minY = min(minY, y)
                maxX = max(maxX, x)
                maxY = max(maxY, y)
            }
        }
    }

    if minX > maxX || minY > maxY {
        return CGRect(x: 0, y: 0, width: width, height: height)
    }
    return CGRect(x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1)
}

func writePNG(_ image: NSImage, to url: URL) {
    guard let tiff = image.tiffRepresentation,
          let rep = NSBitmapImageRep(data: tiff),
          let data = rep.representation(using: .png, properties: [:]) else {
        fatalError("could not encode png: \(url.path)")
    }
    try! data.write(to: url)
}

func luminance(_ p: Pixel) -> Int {
    (Int(p.r) + Int(p.g) + Int(p.b)) / 3
}

func image(from pixels: [Pixel], width: Int, height: Int) -> CGImage {
    var data = [UInt8](repeating: 0, count: width * height * 4)
    for (i, p) in pixels.enumerated() {
        let index = i * 4
        data[index] = p.r
        data[index + 1] = p.g
        data[index + 2] = p.b
        data[index + 3] = p.a
    }

    let colorSpace = CGColorSpaceCreateDeviceRGB()
    let provider = CGDataProvider(data: Data(data) as CFData)!
    return CGImage(
        width: width,
        height: height,
        bitsPerComponent: 8,
        bitsPerPixel: 32,
        bytesPerRow: width * 4,
        space: colorSpace,
        bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
        provider: provider,
        decode: nil,
        shouldInterpolate: true,
        intent: .defaultIntent
    )!
}

func cleanedAppIconImage(_ source: CGImage) -> CGImage {
    let width = source.width
    let height = source.height
    var sourcePixels = pixels(from: source)
    var visited = [Bool](repeating: false, count: width * height)
    var largestDarkComponent = [Int]()

    func isDark(_ index: Int) -> Bool {
        let p = sourcePixels[index]
        return p.a > 8 && luminance(p) < 32
    }

    func neighbors(_ index: Int) -> [Int] {
        let x = index % width
        let y = index / width
        var result = [Int]()
        if x > 0 { result.append(index - 1) }
        if x + 1 < width { result.append(index + 1) }
        if y > 0 { result.append(index - width) }
        if y + 1 < height { result.append(index + width) }
        return result
    }

    for index in 0..<sourcePixels.count where !visited[index] && isDark(index) {
        var queue = [index]
        var head = 0
        var component = [Int]()
        visited[index] = true

        while head < queue.count {
            let current = queue[head]
            head += 1
            component.append(current)

            for next in neighbors(current) where !visited[next] && isDark(next) {
                visited[next] = true
                queue.append(next)
            }
        }

        if component.count > largestDarkComponent.count {
            largestDarkComponent = component
        }
    }

    if largestDarkComponent.count > (width * height) / 20 {
        for index in largestDarkComponent {
            sourcePixels[index] = Pixel(r: 0, g: 0, b: 0, a: 0)
        }
    }

    var edgeQueue = [Int]()
    var edgeVisited = [Bool](repeating: false, count: width * height)
    func enqueueEdge(_ index: Int) {
        guard !edgeVisited[index] else { return }
        let p = sourcePixels[index]
        if p.a <= 8 || luminance(p) > 245 {
            edgeVisited[index] = true
            edgeQueue.append(index)
        }
    }

    for x in 0..<width {
        enqueueEdge(x)
        enqueueEdge((height - 1) * width + x)
    }
    for y in 0..<height {
        enqueueEdge(y * width)
        enqueueEdge(y * width + width - 1)
    }

    var head = 0
    while head < edgeQueue.count {
        let current = edgeQueue[head]
        head += 1
        for next in neighbors(current) {
            enqueueEdge(next)
        }
    }

    for index in edgeVisited.indices where edgeVisited[index] {
        sourcePixels[index] = Pixel(r: 0, g: 0, b: 0, a: 0)
    }

    return image(from: sourcePixels, width: width, height: height)
}

func renderTemplateMenuIcon(source: CGImage, to url: URL) {
    let sourceWidth = source.width
    let sourceHeight = source.height
    let sourcePixels = pixels(from: source)
    let bounds = foregroundBounds(sourcePixels, width: sourceWidth, height: sourceHeight, threshold: 245)
        .insetBy(dx: -18, dy: -18)
        .intersection(CGRect(x: 0, y: 0, width: sourceWidth, height: sourceHeight))

    let width = 96
    let height = 64
    var out = [UInt8](repeating: 0, count: width * height * 4)
    let scale = min(84.0 / bounds.width, 60.0 / bounds.height)
    let drawWidth = bounds.width * scale
    let drawHeight = bounds.height * scale
    let offsetX = (Double(width) - drawWidth) / 2.0
    let offsetY = (Double(height) - drawHeight) / 2.0

    for y in 0..<height {
        for x in 0..<width {
            let sourceX = Int((Double(x) - offsetX) / scale + bounds.minX)
            let sourceY = Int((Double(y) - offsetY) / scale + bounds.minY)
            guard sourceX >= 0, sourceX < sourceWidth, sourceY >= 0, sourceY < sourceHeight else {
                continue
            }
            let p = sourcePixels[sourceY * sourceWidth + sourceX]
            let lum = (Int(p.r) + Int(p.g) + Int(p.b)) / 3
            let alpha = UInt8(max(0, min(255, (245 - lum) * 2)))
            let index = (y * width + x) * 4
            out[index] = 0
            out[index + 1] = 0
            out[index + 2] = 0
            out[index + 3] = min(p.a, alpha)
        }
    }

    let colorSpace = CGColorSpaceCreateDeviceRGB()
    let provider = CGDataProvider(data: Data(out) as CFData)!
    let image = CGImage(
        width: width,
        height: height,
        bitsPerComponent: 8,
        bitsPerPixel: 32,
        bytesPerRow: width * 4,
        space: colorSpace,
        bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
        provider: provider,
        decode: nil,
        shouldInterpolate: true,
        intent: .defaultIntent
    )!
    let nsImage = NSImage(cgImage: image, size: NSSize(width: width, height: height))
    writePNG(nsImage, to: url)
}

func renderAppIconPNGs(source rawSource: CGImage, iconsetURL: URL) {
    try! FileManager.default.createDirectory(at: iconsetURL, withIntermediateDirectories: true)
    let source = cleanedAppIconImage(rawSource)
    let sourcePixels = pixels(from: source)
    let rawBounds = foregroundBounds(sourcePixels, width: source.width, height: source.height, threshold: 252)
        .insetBy(dx: -35, dy: -35)
        .intersection(CGRect(x: 0, y: 0, width: source.width, height: source.height))
    let side = max(rawBounds.width, rawBounds.height)
    let crop = CGRect(
        x: max(0, rawBounds.midX - side / 2),
        y: max(0, rawBounds.midY - side / 2),
        width: min(CGFloat(source.width), side),
        height: min(CGFloat(source.height), side)
    ).integral

    guard let cropped = source.cropping(to: crop) else {
        fatalError("could not crop app icon")
    }

    let sizes: [(String, Int)] = [
        ("icon_16x16.png", 16),
        ("icon_16x16@2x.png", 32),
        ("icon_32x32.png", 32),
        ("icon_32x32@2x.png", 64),
        ("icon_128x128.png", 128),
        ("icon_128x128@2x.png", 256),
        ("icon_256x256.png", 256),
        ("icon_256x256@2x.png", 512),
        ("icon_512x512.png", 512),
        ("icon_512x512@2x.png", 1024),
    ]

    for (name, size) in sizes {
        let image = NSImage(size: NSSize(width: size, height: size))
        image.lockFocus()
        NSGraphicsContext.current?.imageInterpolation = .high
        NSColor.white.setFill()
        NSBezierPath(
            roundedRect: NSRect(x: 0, y: 0, width: size, height: size),
            xRadius: CGFloat(size) * 0.22,
            yRadius: CGFloat(size) * 0.22
        ).fill()

        let padding = CGFloat(size) * 0.04
        NSImage(cgImage: cropped, size: NSSize(width: cropped.width, height: cropped.height))
            .draw(in: NSRect(
                x: padding,
                y: padding,
                width: CGFloat(size) - padding * 2.0,
                height: CGFloat(size) - padding * 2.0
            ))
        image.unlockFocus()
        writePNG(image, to: iconsetURL.appendingPathComponent(name))
    }
}

let menuImage = cgImage(from: menuURL)
let appImage = cgImage(from: appURL)
renderTemplateMenuIcon(
    source: menuImage,
    to: outputURL.appendingPathComponent("MenuBarIconTemplate.png")
)

let iconsetURL = outputURL.appendingPathComponent("AppIcon.iconset")
try? FileManager.default.removeItem(at: iconsetURL)
renderAppIconPNGs(source: appImage, iconsetURL: iconsetURL)

let icnsURL = outputURL.appendingPathComponent("AppIcon.icns")
try? FileManager.default.removeItem(at: icnsURL)
let process = Process()
process.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
process.arguments = ["-c", "icns", iconsetURL.path, "-o", icnsURL.path]
try process.run()
process.waitUntilExit()
if process.terminationStatus != 0 {
    fatalError("iconutil failed")
}
