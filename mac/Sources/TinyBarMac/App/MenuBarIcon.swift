#if os(macOS)
import AppKit
import TinyBarCore

/// The menu-bar icon's images (mac-app-ux.md 3.3): one monochrome template
/// image per state, so macOS tints it for light and dark menu bars and for
/// the highlighted state.
///
/// SF Symbols by default. The custom glyphs (the actual bar, drawn from the
/// SVGs in 3.3) are the fallback, for the whole set at once: if any state's
/// symbol is missing on this Mac, every state uses its glyph, so the five
/// never mix weights and sizes (criterion 25). They can also be tried
/// instead on a real Mac with
/// `defaults write com.minibar.MiniBarMac IconStyle glyphs` (then relaunch),
/// to compare them in the menu bar without rebuilding.
///
/// Not tied to the main actor: an image's drawing handler may run wherever
/// AppKit draws it.
enum MenuBarIcon {
    /// 3.3: "start at 14 pt, regular, and tune on a real Mac". *Unverified.*
    static var symbolConfiguration: NSImage.SymbolConfiguration {
        NSImage.SymbolConfiguration(pointSize: 14, weight: .regular)
    }

    static var useGlyphs: Bool {
        UserDefaults.standard.string(forKey: "IconStyle") == "glyphs"
    }

    /// Every state's SF Symbol exists on this Mac. Checked once.
    static let symbolsAvailable: Bool = IconState.allCases.allSatisfy {
        NSImage(systemSymbolName: $0.symbolName, accessibilityDescription: nil) != nil
    }

    static func image(for state: IconState) -> NSImage {
        if !useGlyphs, symbolsAvailable,
           let symbol = NSImage(systemSymbolName: state.symbolName, accessibilityDescription: state.accessibilityLabel)?
            .withSymbolConfiguration(symbolConfiguration) {
            symbol.isTemplate = true
            return symbol
        }
        return glyph(for: state)
    }

    // MARK: - The custom glyphs

    /// 22 × 16 pt, drawn in SVG coordinates (y down) from mac-app-ux.md 3.3.
    static func glyph(for state: IconState) -> NSImage {
        let image = NSImage(size: NSSize(width: 22, height: 16), flipped: true) { _ in
            NSColor.black.setFill()
            NSColor.black.setStroke()
            switch state {
            case .connected:
                outline().stroke()
            case .onCall:
                // Filled, with the column divider cut out.
                let body = NSBezierPath(roundedRect: NSRect(x: 1, y: 3, width: 20, height: 10), xRadius: 3, yRadius: 3)
                body.append(NSBezierPath(rect: NSRect(x: 14.75, y: 3, width: 1, height: 10)))
                body.windingRule = .evenOdd
                body.fill()
            case .notConnected:
                outline().stroke()
                // A clear gap around the slash, then the slash.
                let gap = slash()
                gap.lineWidth = 4
                NSGraphicsContext.current?.compositingOperation = .clear
                gap.stroke()
                NSGraphicsContext.current?.compositingOperation = .sourceOver
                slash().stroke()
            case .paused:
                outline().stroke()
                NSBezierPath(roundedRect: NSRect(x: 6.5, y: 5.5, width: 1.5, height: 5), xRadius: 0.5, yRadius: 0.5).fill()
                NSBezierPath(roundedRect: NSRect(x: 9, y: 5.5, width: 1.5, height: 5), xRadius: 0.5, yRadius: 0.5).fill()
            case .needsYou:
                outline().stroke()
                NSBezierPath(roundedRect: NSRect(x: 7.5, y: 5, width: 2, height: 3.25), xRadius: 1, yRadius: 1).fill()
                NSBezierPath(ovalIn: NSRect(x: 7.5, y: 8.85, width: 2, height: 2)).fill()
            }
            return true
        }
        image.isTemplate = true
        image.accessibilityDescription = state.accessibilityLabel
        return image
    }

    /// The bar in outline: the body and the info column's divider, 1.5 pt.
    private static func outline() -> NSBezierPath {
        let path = NSBezierPath(roundedRect: NSRect(x: 1.75, y: 3.75, width: 18.5, height: 8.5), xRadius: 2.25, yRadius: 2.25)
        path.move(to: NSPoint(x: 15.25, y: 3.75))
        path.line(to: NSPoint(x: 15.25, y: 12.25))
        path.lineWidth = 1.5
        return path
    }

    private static func slash() -> NSBezierPath {
        let path = NSBezierPath()
        path.move(to: NSPoint(x: 3, y: 1.25))
        path.line(to: NSPoint(x: 19, y: 14.75))
        path.lineWidth = 1.5
        path.lineCapStyle = .round
        return path
    }
}
#endif
