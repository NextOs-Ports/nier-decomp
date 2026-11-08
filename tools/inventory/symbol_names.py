"""Normalize only known guest-entry wrappers, without attributing C++ helpers."""
def canonical_guest_symbol(symbol: str) -> str:
    name = symbol.removeprefix('__imp__')
    if name.startswith('nier_native_sub_'):
        name = name.removeprefix('nier_native_')
    return name
