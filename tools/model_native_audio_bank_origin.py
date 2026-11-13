"""Deterministic model of Rayman2 retail-CD loaded sound-bank inverse lookup.

Research only. Synthetic structures, no game memory or proprietary audio.
The C++ diagnostic uses bounded ReadProcessMemory instead of these dicts.
"""
from __future__ import annotations
from dataclasses import dataclass


@dataclass(frozen=True)
class BankDescriptor:
    bank_id: int
    state: int
    head: int
    next: int


@dataclass(frozen=True)
class BankHead:
    resource_first: int
    resource_count: int


@dataclass(frozen=True)
class BankOrigin:
    bank_id: int = -1
    resource_index: int = -1
    candidates: int = 0
    list_complete: bool = False
    nodes_scanned: int = 0


def invert_native_loaded_bank_registry(
    first_node: int,
    resource_ptr: int,
    descriptors: dict[int, BankDescriptor],
    heads: dict[int, BankHead],
    max_nodes: int = 512,
) -> BankOrigin:
    """Only an exhaustive, unique match may produce a bank/index pair."""
    visited: set[int] = set()
    matches: list[tuple[int, int]] = []
    if resource_ptr < 0x10000 or max_nodes <= 0:
        return BankOrigin()
    node = first_node
    for _ in range(max_nodes):
        if node == 0:
            if len(matches) == 1:
                return BankOrigin(*matches[0], len(matches), True, len(visited))
            return BankOrigin(candidates=len(matches), list_complete=True,
                              nodes_scanned=len(visited))
        if node < 0x10000 or node in visited or node not in descriptors:
            return BankOrigin(candidates=len(matches), nodes_scanned=len(visited))
        visited.add(node)
        descriptor = descriptors[node]
        if descriptor.state == 1:  # SND_BANK_Loaded
            if descriptor.head < 0x10000 or descriptor.head not in heads:
                return BankOrigin(candidates=len(matches),
                                  nodes_scanned=len(visited))
            head = heads[descriptor.head]
            if not (0 <= head.resource_count <= 65536):
                return BankOrigin(candidates=len(matches),
                                  nodes_scanned=len(visited))
            if head.resource_count and head.resource_first < 0x10000:
                return BankOrigin(candidates=len(matches),
                                  nodes_scanned=len(visited))
            difference = resource_ptr - head.resource_first
            if (head.resource_count and difference >= 0
                    and difference % 92 == 0
                    and difference // 92 < head.resource_count):
                matches.append((descriptor.bank_id, difference // 92))
        node = descriptor.next
    # Exhausted the bound before the native list ended: no provenance.
    return BankOrigin(candidates=len(matches),nodes_scanned=len(visited))
