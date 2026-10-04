/// Marks a body that a builder still has to write.
///
/// Every stub in the package calls this, so `grep -rn "unimplemented(" Sources`
/// lists the work that's left. It's `package` access: usable from both targets,
/// but not part of TinyBarCore's public interface.
@inline(never)
package func unimplemented(
    _ what: String = #function,
    file: StaticString = #fileID,
    line: UInt = #line
) -> Never {
    fatalError("Not implemented yet: \(what)", file: file, line: line)
}
