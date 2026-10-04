#if os(macOS)
import SwiftUI

/// A placeholder for a view a builder still has to write. A view body can't
/// call `unimplemented()` (an opaque `some View` needs a real type), so stub
/// views show this instead. `grep -rn "Unimplemented" Sources` finds them.
struct UnimplementedView: View {
    let name: String

    var body: some View {
        Text(verbatim: "\(name) isn't built yet.")
            .padding(40)
    }
}
#endif
