/// The configuration page is served over plain HTTP, where the Clipboard API is
/// not available; selecting the field and the old copy command work everywhere.
export function copyFrom(input) {
    input.select();
    input.setSelectionRange(0, input.value.length);
    return document.execCommand("copy");
}
