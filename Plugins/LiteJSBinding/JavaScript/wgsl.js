// Template-string serialization, not shader translation or engine behavior.
export function wgsl(strings, ...values) {
    let source = strings[0];
    for (let i = 0; i < values.length; ++i) source += String(values[i]) + strings[i + 1];
    return source;
}
