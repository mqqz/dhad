#!/usr/bin/env python3

import argparse
import pathlib
from collections import defaultdict, deque
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from typing import NamedTuple


@dataclass(frozen=True)
class Symbol:
    kind: str  # "NT" or "TOK"
    value: str


@dataclass
class Production:
    lhs: str
    rhs: list[Symbol]


class LR0Item(NamedTuple):
    production: int
    dot: int


class LR1Item(NamedTuple):
    production: int
    dot: int
    lookahead: str


@dataclass
class CLIArgs:
    grammar_def: str
    nonterminals: str
    output: str
    force: bool


def parse_args() -> CLIArgs:
    parser = argparse.ArgumentParser(
        description="Generate parser tables and LR(0) states from the grammar definition."
    )
    _ = parser.add_argument("--grammar-def", required=True, help="Path to grammar.def.")
    _ = parser.add_argument("--nonterminals", required=True, help="Path to nonterminals.def.")
    _ = parser.add_argument("--output", required=True, help="Destination header.")
    _ = parser.add_argument(
        "--force",
        action="store_true",
        help="Force regeneration even if the contents have not changed.",
    )
    ns = parser.parse_args()
    return CLIArgs(
        grammar_def=str(ns.grammar_def),
        nonterminals=str(ns.nonterminals),
        output=str(ns.output),
        force=bool(ns.force),
    )


def needs_regeneration(output_path: pathlib.Path, inputs: Sequence[pathlib.Path]) -> bool:
    if not output_path.exists():
        return True
    out_mtime = output_path.stat().st_mtime
    for path in inputs:
        if not path.exists():
            continue
        if path.stat().st_mtime > out_mtime:
            return True
    return False


def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_line_comments(text: str) -> str:
    lines: list[str] = []
    for raw_line in text.splitlines():
        if "//" in raw_line:
            raw_line = raw_line.split("//", 1)[0]
        lines.append(raw_line)
    return "\n".join(lines)


def extract_arguments(source: str, start_idx: int) -> tuple[str, int]:
    idx = start_idx
    while idx < len(source) and source[idx].isspace():
        idx += 1
    if idx >= len(source) or source[idx] != "(":
        raise ValueError("Expected '(' in macro invocation")
    depth = 0
    i = idx
    while i < len(source):
        char = source[i]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return source[idx + 1 : i], i + 1
        i += 1
    raise ValueError("Unbalanced parentheses in grammar definition")


def split_args(arg_string: str) -> list[str]:
    args: list[str] = []
    current: list[str] = []
    depth = 0
    for char in arg_string:
        if char == "," and depth == 0:
            arg = "".join(current).strip()
            if arg:
                args.append(arg)
            current = []
            continue
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        current.append(char)
    tail = "".join(current).strip()
    if tail:
        args.append(tail)
    return args


def extract_macro_arg(expr: str, macro: str) -> str:
    expr = expr.strip()
    prefix = f"{macro}("
    if not expr.startswith(prefix) or not expr.endswith(")"):
        raise ValueError(f"Malformed {macro} symbol: {expr}")
    return expr[len(prefix) : -1].strip()


def parse_symbol(token: str) -> Symbol:
    token = token.strip()
    if token.startswith("NT"):
        return Symbol("NT", extract_macro_arg(token, "NT"))
    if token.startswith("TOK"):
        return Symbol("TOK", extract_macro_arg(token, "TOK"))
    raise ValueError(f"Unsupported symbol token: {token}")


def parse_nonterminals(path: pathlib.Path) -> list[str]:
    names: list[str] = []
    for raw_line in read_text(path).splitlines():
        line = raw_line.strip()
        if not line or line.startswith("//"):
            continue
        if not line.startswith("NONTERM"):
            continue
        start = line.find("(")
        end = line.find(")", start)
        if start == -1 or end == -1:
            continue
        names.append(line[start + 1 : end].strip())
    return names


def parse_grammar(grammar_path: pathlib.Path) -> list[Production]:
    source = strip_line_comments(read_text(grammar_path))
    productions: list[Production] = []
    idx = 0
    while idx < len(source):
        next_rule = source.find("RULE(", idx)
        next_empty = source.find("RULE_EMPTY(", idx)
        if next_rule == -1 and next_empty == -1:
            break
        if next_empty != -1 and (next_rule == -1 or next_empty < next_rule):
            macro = "RULE_EMPTY"
            start = next_empty
        else:
            macro = "RULE"
            start = next_rule
        paren_idx = start + len(macro)
        args_str, end_idx = extract_arguments(source, paren_idx)
        arg_list = split_args(args_str)
        if macro == "RULE":
            if len(arg_list) < 2:
                raise ValueError("RULE requires a left-hand side and action")
            lhs = arg_list[0].strip()
            rhs_symbols = [parse_symbol(arg) for arg in arg_list[2:]]
        else:
            if len(arg_list) < 2:
                raise ValueError("RULE_EMPTY expects a left-hand side and action")
            lhs = arg_list[0].strip()
            rhs_symbols = []
        productions.append(Production(lhs=lhs, rhs=rhs_symbols))
        idx = end_idx
        while idx < len(source) and source[idx] in " \r\n\t,":
            idx += 1
    return productions


def validate_grammar(productions: Sequence[Production], nonterms: Sequence[str]) -> None:
    nonterm_set = set(nonterms)
    if not productions:
        raise ValueError("Grammar has no productions")
    if productions[0].lhs != "AugmentedStart":
        raise ValueError("First production must be AugmentedStart -> Program")
    for prod in productions:
        if prod.lhs not in nonterm_set:
            raise ValueError(f"Unknown non-terminal in production: {prod.lhs}")
        for symbol in prod.rhs:
            if symbol.kind == "NT" and symbol.value not in nonterm_set:
                raise ValueError(f"Unknown non-terminal reference: {symbol.value}")


def build_production_index(productions: Sequence[Production]) -> dict[str, list[int]]:
    mapping: dict[str, list[int]] = defaultdict(list)
    for idx, prod in enumerate(productions):
        mapping[prod.lhs].append(idx)
    return mapping


def compute_nullable(productions: Sequence[Production], nonterminals: Sequence[str]) -> set[str]:
    nullable: set[str] = set()
    changed = True
    while changed:
        changed = False
        for prod in productions:
            if prod.lhs in nullable:
                continue
            if not prod.rhs:
                nullable.add(prod.lhs)
                changed = True
                continue
            if all(symbol.kind == "NT" and symbol.value in nullable for symbol in prod.rhs):
                nullable.add(prod.lhs)
                changed = True
    return nullable


def compute_first_sets(
    productions: Sequence[Production], nonterminals: Sequence[str], nullable: set[str]
) -> dict[str, set[str]]:
    first: dict[str, set[str]] = {nt: set() for nt in nonterminals}
    changed = True
    while changed:
        changed = False
        for prod in productions:
            idx = 0
            while True:
                if idx >= len(prod.rhs):
                    break
                symbol = prod.rhs[idx]
                if symbol.kind == "TOK":
                    if symbol.value not in first[prod.lhs]:
                        first[prod.lhs].add(symbol.value)
                        changed = True
                    break
                before = len(first[prod.lhs])
                first[prod.lhs].update(first[symbol.value])
                if len(first[prod.lhs]) > before:
                    changed = True
                if symbol.value not in nullable:
                    break
                idx += 1
    return first


def first_of_suffix(
    rhs: Sequence[Symbol],
    start_idx: int,
    lookahead: str,
    nullable: set[str],
    first_sets: dict[str, set[str]],
) -> set[str]:
    result: set[str] = set()
    nullable_suffix = True
    for symbol in rhs[start_idx:]:
        if symbol.kind == "TOK":
            result.add(symbol.value)
            nullable_suffix = False
            break
        result.update(first_sets[symbol.value])
        if symbol.value not in nullable:
            nullable_suffix = False
            break
    if nullable_suffix:
        result.add(lookahead)
    return result


def lr1_closure(
    items: Iterable[LR1Item],
    productions: Sequence[Production],
    prod_index: dict[str, list[int]],
    nullable: set[str],
    first_sets: dict[str, set[str]],
) -> frozenset[LR1Item]:
    result: set[LR1Item] = set(items)
    queue: deque[LR1Item] = deque(result)
    while queue:
        item = queue.popleft()
        prod = productions[item.production]
        if item.dot >= len(prod.rhs):
            continue
        symbol = prod.rhs[item.dot]
        if symbol.kind != "NT":
            continue
        lookaheads = first_of_suffix(prod.rhs, item.dot + 1, item.lookahead, nullable, first_sets)
        for prod_idx in prod_index[symbol.value]:
            for la in lookaheads:
                new_item = LR1Item(prod_idx, 0, la)
                if new_item not in result:
                    result.add(new_item)
                    queue.append(new_item)
    return frozenset(result)


def compute_lr1_states(
    productions: Sequence[Production],
    prod_index: dict[str, list[int]],
    nullable: set[str],
    first_sets: dict[str, set[str]],
) -> tuple[list[frozenset[LR1Item]], list[dict[Symbol, int]]]:
    start_item = LR1Item(0, 0, "ENDF")
    start_state = lr1_closure([start_item], productions, prod_index, nullable, first_sets)
    states: list[frozenset[LR1Item]] = [start_state]
    transitions: list[dict[Symbol, int]] = [{}]
    state_map: dict[frozenset[LR1Item], int] = {start_state: 0}
    pending: deque[int] = deque([0])

    while pending:
        state_idx = pending.popleft()
        state = states[state_idx]
        symbol_moves: dict[Symbol, list[LR1Item]] = defaultdict(list)
        for item in state:
            prod = productions[item.production]
            if item.dot >= len(prod.rhs):
                continue
            symbol = prod.rhs[item.dot]
            symbol_moves[symbol].append(LR1Item(item.production, item.dot + 1, item.lookahead))
        current_transitions: dict[Symbol, int] = {}
        for symbol, move_items in symbol_moves.items():
            goto_state = lr1_closure(move_items, productions, prod_index, nullable, first_sets)
            dest = state_map.get(goto_state)
            if dest is None:
                dest = len(states)
                state_map[goto_state] = dest
                states.append(goto_state)
                transitions.append({})
                pending.append(dest)
            current_transitions[symbol] = dest
        transitions[state_idx] = current_transitions
    return states, transitions


def lr0_closure(
    items: Iterable[LR0Item],
    productions: Sequence[Production],
    prod_index: dict[str, list[int]],
) -> frozenset[LR0Item]:
    result: set[LR0Item] = set(items)
    queue: list[LR0Item] = list(result)
    while queue:
        item = queue.pop()
        prod = productions[item.production]
        if item.dot >= len(prod.rhs):
            continue
        symbol = prod.rhs[item.dot]
        if symbol.kind != "NT":
            continue
        for prod_idx in prod_index[symbol.value]:
            new_item = LR0Item(prod_idx, 0)
            if new_item not in result:
                result.add(new_item)
                queue.append(new_item)
    return frozenset(result)


def compute_lr0_states(
    productions: Sequence[Production],
    prod_index: dict[str, list[int]],
) -> list[frozenset[LR0Item]]:
    start_state = lr0_closure([LR0Item(0, 0)], productions, prod_index)
    states: list[frozenset[LR0Item]] = [start_state]
    state_map: dict[frozenset[LR0Item], int] = {start_state: 0}
    pending: deque[frozenset[LR0Item]] = deque([start_state])

    while pending:
        state = pending.popleft()
        transitions: dict[Symbol, list[LR0Item]] = defaultdict(list)
        for item in state:
            prod = productions[item.production]
            if item.dot >= len(prod.rhs):
                continue
            symbol = prod.rhs[item.dot]
            transitions[symbol].append(LR0Item(item.production, item.dot + 1))
        for symbol in sorted(transitions.keys(), key=lambda s: (s.kind, s.value)):
            goto_state = lr0_closure(transitions[symbol], productions, prod_index)
            if goto_state not in state_map:
                state_map[goto_state] = len(states)
                states.append(goto_state)
                pending.append(goto_state)
    return states


def symbol_to_text(symbol: Symbol) -> str:
    return symbol.value if symbol.kind == "NT" else f"'{symbol.value}'"


def format_item_comment(production: Production, dot: int) -> str:
    parts: list[str] = []
    rhs = production.rhs
    for position in range(len(rhs) + 1):
        if position == dot:
            parts.append("<dot>")
        if position < len(rhs):
            parts.append(symbol_to_text(rhs[position]))
    if not rhs:
        parts.append("<epsilon>")
    return f"{production.lhs} -> {' '.join(parts)}"


def build_state_tables(
    states: Sequence[frozenset[LR0Item]],
    productions: Sequence[Production],
) -> tuple[list[tuple[LR0Item, str]], list[tuple[int, int, str]]]:
    item_entries: list[tuple[LR0Item, str]] = []
    state_ranges: list[tuple[int, int, str]] = []
    for state in states:
        sorted_items = sorted(state, key=lambda item: (item.production, item.dot))
        start = len(item_entries)
        descriptions: list[str] = []
        for item in sorted_items:
            desc = format_item_comment(productions[item.production], item.dot)
            item_entries.append((item, desc))
            descriptions.append(desc)
        count = len(sorted_items)
        summary = "; ".join(descriptions) if descriptions else "<empty>"
        state_ranges.append((start, count, summary))
    return item_entries, state_ranges


def format_lr0_items(entries: Sequence[tuple[LR0Item, str]]) -> str:
    lines = [f"constexpr std::array<LR0Item, {len(entries)}> kLR0Items = {{"]
    for item, desc in entries:
        lines.append(f"    LR0Item{{{item.production}, {item.dot}}}, // {desc}")
    lines.append("};")
    return "\n".join(lines)


def format_lr0_states(entries: Sequence[tuple[int, int, str]]) -> str:
    lines = [f"constexpr std::array<LR0StateRange, {len(entries)}> kLR0States = {{"]
    for idx, (offset, count, summary) in enumerate(entries):
        lines.append(f"    LR0StateRange{{{offset}, {count}}}, // State {idx}: {summary}")
    lines.append("};")
    return "\n".join(lines)


def merge_lalr_states(
    lr1_states: Sequence[frozenset[LR1Item]],
    transitions: Sequence[dict[Symbol, int]],
) -> tuple[list[dict[LR0Item, set[str]]], list[dict[Symbol, int]], list[int]]:
    kernel_to_id: dict[frozenset[LR0Item], int] = {}
    kernel_items: list[dict[LR0Item, set[str]]] = []
    kernel_transitions: list[dict[Symbol, int]] = []
    state_kernel_id: list[int] = []

    for state in lr1_states:
        kernel = frozenset(
            LR0Item(item.production, item.dot)
            for item in state
            if item.production == 0 or item.dot > 0
        )
        if kernel not in kernel_to_id:
            kernel_to_id[kernel] = len(kernel_items)
            kernel_items.append(defaultdict(set))
            kernel_transitions.append({})
        state_kernel_id.append(kernel_to_id[kernel])

    for state_idx, state in enumerate(lr1_states):
        kernel_id = state_kernel_id[state_idx]
        for item in state:
            if item.production == 0 or item.dot > 0:
                lr0_item = LR0Item(item.production, item.dot)
                kernel_items[kernel_id].setdefault(lr0_item, set()).add(item.lookahead)

    for state_idx, trans in enumerate(transitions):
        src_kernel = state_kernel_id[state_idx]
        for symbol, dest_state_idx in trans.items():
            dest_kernel = state_kernel_id[dest_state_idx]
            existing = kernel_transitions[src_kernel].get(symbol)
            if existing is not None and existing != dest_kernel:
                raise ValueError("Inconsistent LALR transition detected")
            kernel_transitions[src_kernel][symbol] = dest_kernel

    return kernel_items, kernel_transitions, state_kernel_id


def expand_lalr_state(
    kernel_item_map: dict[LR0Item, set[str]],
    productions: Sequence[Production],
    prod_index: dict[str, list[int]],
    nullable: set[str],
    first_sets: dict[str, set[str]],
) -> frozenset[LR1Item]:
    seed_items = [
        LR1Item(item.production, item.dot, lookahead)
        for item, lookaheads in kernel_item_map.items()
        for lookahead in lookaheads
    ]
    return lr1_closure(seed_items, productions, prod_index, nullable, first_sets)


def build_action_goto_maps(
    kernel_items: Sequence[dict[LR0Item, set[str]]],
    kernel_transitions: Sequence[dict[Symbol, int]],
    productions: Sequence[Production],
    prod_index: dict[str, list[int]],
    nullable: set[str],
    first_sets: dict[str, set[str]],
) -> tuple[list[dict[str, tuple[str, int | None]]], list[dict[str, int]]]:
    expanded_states = [
        expand_lalr_state(kernel_item_map, productions, prod_index, nullable, first_sets)
        for kernel_item_map in kernel_items
    ]
    action_rows: list[dict[str, tuple[str, int | None]]] = []
    goto_rows: list[dict[str, int]] = []

    for state_idx, state in enumerate(expanded_states):
        action_map: dict[str, tuple[str, int | None]] = {}
        goto_map: dict[str, int] = {}
        for symbol, dest in kernel_transitions[state_idx].items():
            if symbol.kind == "TOK":
                existing = action_map.get(symbol.value)
                entry = ("shift", dest)
                if existing:
                    if existing[0] == "shift":
                        entry = existing
                    elif entry[0] == "shift":
                        entry = entry
                    else:
                        raise ValueError(f"Reduce/Reduce conflict on {symbol.value} in state {state_idx}")
                action_map[symbol.value] = entry
            else:
                if symbol.value in goto_map and goto_map[symbol.value] != dest:
                    raise ValueError(f"Inconsistent goto for {symbol.value} in state {state_idx}")
                goto_map[symbol.value] = dest
        for item in state:
            prod = productions[item.production]
            if item.dot != len(prod.rhs):
                continue
            if item.production == 0:
                if item.lookahead != "ENDF":
                    raise ValueError("Augmented start item expects ENDF lookahead")
                entry = ("accept", None)
            else:
                entry = ("reduce", item.production)
            existing = action_map.get(item.lookahead)
            if existing:
                if existing[0] == "shift":
                    entry = existing
                elif entry[0] == "shift":
                    entry = entry
                elif existing != entry:
                    raise ValueError(
                        f"Reduce/Reduce conflict on {item.lookahead} in state {state_idx}: {existing} vs {entry}"
                    )
            action_map[item.lookahead] = entry
        action_rows.append(action_map)
        goto_rows.append(goto_map)

    return action_rows, goto_rows


def format_action_table(action_rows: Sequence[dict[str, tuple[str, int | None]]]) -> str:
    lines = ["constexpr auto kGeneratedActionTable = ACTION_TABLE("]
    for idx, row in enumerate(action_rows):
        entries = []
        sorted_tokens = sorted(row.keys())
        for token in sorted_tokens:
            action_type, value = row[token]
            if action_type == "shift":
                expr = f"makeShift({value})"
            elif action_type == "reduce":
                expr = f"makeReduce({value})"
            elif action_type == "accept":
                expr = "makeAccept()"
            else:
                raise ValueError(f"Unknown action type: {action_type}")
            entries.append(f"        ACTION_ENTRY({token}, {expr})")
        if entries:
            lines.append("    ACTION_ROW(")
            for entry_idx, entry in enumerate(entries):
                suffix = "," if entry_idx + 1 < len(entries) else ""
                lines.append(f"{entry}{suffix}")
            lines.append(f"    ){',' if idx + 1 < len(action_rows) else ''}")
        else:
            lines.append(f"    ACTION_ROW(){',' if idx + 1 < len(action_rows) else ''}")
    lines.append(");")
    return "\n".join(lines)


def format_goto_table(goto_rows: Sequence[dict[str, int]]) -> str:
    lines = ["constexpr auto kGeneratedGotoTable = GOTO_TABLE("]
    for idx, row in enumerate(goto_rows):
        ordered = sorted(row.items())
        entries = [f"        GOTO_ENTRY({symbol}, {dest})" for symbol, dest in ordered]
        if entries:
            lines.append("    GOTO_ROW(")
            for entry_idx, entry in enumerate(entries):
                suffix = "," if entry_idx + 1 < len(entries) else ""
                lines.append(f"{entry}{suffix}")
            lines.append(f"    ){',' if idx + 1 < len(goto_rows) else ''}")
        else:
            lines.append(f"    GOTO_ROW(){',' if idx + 1 < len(goto_rows) else ''}")
    lines.append(");")
    return "\n".join(lines)


def render_header(
    grammar_path: pathlib.Path,
    nonterm_path: pathlib.Path,
    items_section: str,
    states_section: str,
    action_section: str,
    goto_section: str,
) -> str:
    lines = [
        "// @generated",
        f"// Derived from: {grammar_path} and {nonterm_path}",
        "#pragma once",
        "#include \"tables.hpp\"",
        "",
        "// NOTE: Include src/parser/tables.hpp before this header so that the",
        "// ACTION_* and GOTO_* macros/macros are available.",
        "",
        "namespace dhad::parser {",
        "",
        items_section,
        "",
        states_section,
        "",
        action_section,
        "",
        goto_section,
        "",
        "} // namespace dhad::parser",
    ]
    return "\n".join(lines).strip() + "\n"


def main() -> int:
    args = parse_args()
    grammar_path = pathlib.Path(args.grammar_def)
    nonterm_path = pathlib.Path(args.nonterminals)
    output_path = pathlib.Path(args.output)
    script_path = pathlib.Path(__file__).resolve()

    watched_inputs = [grammar_path, nonterm_path, script_path]
    if not args.force and not needs_regeneration(output_path, watched_inputs):
        return 0

    output_path.parent.mkdir(parents=True, exist_ok=True)

    productions = parse_grammar(grammar_path)
    nonterminals = parse_nonterminals(nonterm_path)
    validate_grammar(productions, nonterminals)
    prod_index = build_production_index(productions)
    nullable = compute_nullable(productions, nonterminals)
    first_sets = compute_first_sets(productions, nonterminals, nullable)
    lr0_states = compute_lr0_states(productions, prod_index)
    item_entries, state_entries = build_state_tables(lr0_states, productions)
    lr1_states, lr1_transitions = compute_lr1_states(productions, prod_index, nullable, first_sets)
    kernel_items, kernel_transitions, _ = merge_lalr_states(lr1_states, lr1_transitions)
    action_rows, goto_rows = build_action_goto_maps(
        kernel_items, kernel_transitions, productions, prod_index, nullable, first_sets
    )
    items_section = format_lr0_items(item_entries)
    states_section = format_lr0_states(state_entries)
    action_section = format_action_table(action_rows)
    goto_section = format_goto_table(goto_rows)
    header = render_header(
        grammar_path, nonterm_path, items_section, states_section, action_section, goto_section
    )

    existing = output_path.read_text(encoding="utf-8") if output_path.exists() else ""
    if args.force or existing != header:
        _ = output_path.write_text(header, encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
