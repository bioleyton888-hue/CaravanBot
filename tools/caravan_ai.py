"""Caravan strategy engine for CaravanBot.

choose_move(state) takes the plugin's state["state"] dict (on an awaited turn) and returns
(move, reason), where move is the dict to write to move.json (without "turn").

It is a one-move lookahead: every legal move from the plugin's legalMoves (plus discards) is
simulated and the resulting table is scored. Values follow the engine's rule, checked against
702 saved caravans: a caravan's total is the sum over its rows of number value x 2 per King.
"""

KING, QUEEN, JACK, JOKER = 14, 13, 12, 15
SOLD_MIN, SOLD_MAX = 21, 26


def is_number(card):
    return 1 <= card["value"] <= 10 and card["suit"] != 5


def cell_value(cards):
    kings = sum(1 for c in cards[1:] if c["value"] == KING)
    return cards[0]["value"] * 2 ** kings


def board_from_state(s):
    """board[t] = list of rows, each row a list of cards (number card first)."""
    board = []
    for t in s["tracks"]:
        rows = [list(r["cards"]) for r in sorted(t["rows"], key=lambda r: r["row"])]
        board.append(rows)
    return board


def totals(board):
    return [sum(cell_value(row) for row in rows) for rows in board]


def sold(v):
    return SOLD_MIN <= v <= SOLD_MAX


def pair_winner(mine, theirs):
    """'me', 'npc' or None (undecided) for one pair, as the game judges it."""
    if sold(mine) and (not sold(theirs) or mine > theirs):
        return "me"
    if sold(theirs) and (not sold(mine) or theirs > mine):
        return "npc"
    return None


# ---- Simulation ---------------------------------------------------------------------------

def apply_place(board, card, track, row):
    b = [[list(r) for r in rows] for rows in board]
    v = card["value"]
    if is_number(card):
        b[track].append([card])
    elif v == JACK:
        del b[track][row]
    elif v == JOKER:
        target = b[track][row][0]
        b[track][row] = b[track][row] + [card]
        for t in range(6):
            keep = []
            for r_idx, cells in enumerate(b[t]):
                is_target = t == track and r_idx == row
                num = cells[0]
                hit = (num["suit"] == target["suit"]) if target["value"] == 1 else (num["value"] == target["value"])
                if hit and not is_target:
                    continue
                keep.append(cells)
            b[t] = keep
    else:  # King doubles that row; Queen changes direction/suit only
        b[track][row] = b[track][row] + [card]
    return b


def apply_discard_track(board, track):
    b = [[list(r) for r in rows] for rows in board]
    b[track] = []
    return b


# ---- Scoring ------------------------------------------------------------------------------

def caravan_score(v):
    """How good a total is for its owner, ignoring the opponent."""
    if v > SOLD_MAX:
        # Bad, but repairable (a Jack on the overloaded row): not much worse than an empty caravan.
        return -10 - (v - SOLD_MAX) * 0.3
    if v >= SOLD_MIN:
        return 30 + (v - SOLD_MIN) * 1.5
    # Progress toward 21; overshooting is worse than undershooting, so reward steadily.
    return v * 1.0


def evaluate(board, terminal=1000):
    """Score a table from my point of view. Returns (score, note).
    terminal: the bonus/penalty when every pair is decided (the game ends)."""
    vals = totals(board)
    score = 0.0
    wins = {"me": 0, "npc": 0, None: 0}
    for t in range(3):
        mine, theirs = vals[t], vals[5 - t]
        score += caravan_score(mine) - caravan_score(theirs)
        w = pair_winner(mine, theirs)
        wins[w] += 1
        if w == "me":
            score += 25
        elif w == "npc":
            score -= 25
    note = ""
    if wins[None] == 0:  # every pair decided: the game ends right now
        if wins["me"] >= 2:
            score += terminal
            note = "wins the game"
        else:
            score -= terminal
            note = "would end the game as a loss"
    return score, note


FACE_COST = {JACK: 6, KING: 3, QUEEN: 1, JOKER: 7}


# ---- Ringo's reply ------------------------------------------------------------------------
#
# We never see Ringo's hand, only its size. Each card kind is assumed equally common in its deck
# (4 of each value per 54 cards, 2 Jokers), so with h cards it holds a given kind with
# probability 1 - (1 - q)^h. Ringo is assumed to play the most damaging kind it holds; the
# expected score is taken over which kinds it holds. Queens and discards are treated as "no harm".

CARD_FREQ = {v: 4 / 54 for v in list(range(1, 11)) + [JACK, KING]}
CARD_FREQ[JOKER] = 2 / 54


def number_fits(rows, v):
    """Whether a number card of value v may extend a caravan, by direction only (suit unknown)."""
    if not rows:
        return True
    last = rows[-1][0]["value"]
    if v == last:
        return False
    if len(rows) < 2:
        return True
    prev = rows[-2][0]["value"]
    return v > last if last > prev else v < last


def npc_replies(board, kind):
    """Boards Ringo can reach by playing one card of this kind."""
    if kind <= 10:
        card = {"card": f"{kind}?", "value": kind, "suit": 0}
        for t in (3, 4, 5):
            if len(board[t]) < 7 and number_fits(board[t], kind):
                yield apply_place(board, card, t, len(board[t]))
        return
    card = {"card": "?", "value": kind, "suit": 0}
    for t in range(6):
        for r in range(len(board[t])):
            yield apply_place(board, card, t, r)


REPLY_TERMINAL = 150  # a game Ringo could end next turn: a big swing, but not worth wrecking my
                      # own winning caravans to avoid (the alternative position is nearly as lost)


def expected_after_reply(board, opp_hand):
    """My expected score after Ringo's best reply with the cards it is likely to hold."""
    standstill, _ = evaluate(board, REPLY_TERMINAL)
    outcomes = []
    for kind, q in CARD_FREQ.items():
        best = min((evaluate(b, REPLY_TERMINAL)[0] for b in npc_replies(board, kind)), default=standstill)
        if best < standstill:
            p_has = 1 - (1 - q) ** max(opp_hand, 1)
            outcomes.append((best, p_has))
    # Ringo plays its most damaging held kind: walk the kinds from worst for me upward.
    outcomes.sort()
    expected, p_none_better = 0.0, 1.0
    for score, p_has in outcomes:
        expected += p_none_better * p_has * score
        p_none_better *= 1 - p_has
    return expected + p_none_better * standstill


def describe(card, track, row, board_before):
    owner = "my" if track < 3 else "NPC's"
    if is_number(card):
        return f"{card['card']} on {owner} caravan {track}"
    target = board_before[track][row][0]["card"] if row < len(board_before[track]) else "?"
    return f"{card['card']} on {owner} {target} (caravan {track}, row {row})"


REPLY_WEIGHT = 0.5  # how much Ringo's likely reply counts against the table right after my move


def score_after(board, opp_hand, lookahead):
    """Score of the table after my move: final if the game ends, else blended with the expected
    score after Ringo's likely reply (pure expectation made the bot discard sold caravans for fear)."""
    sc, note = evaluate(board)
    if note or not lookahead:
        return sc, note
    return (1 - REPLY_WEIGHT) * sc + REPLY_WEIGHT * expected_after_reply(board, opp_hand), note


def choose_move(s, exclude=(), lookahead=True):
    """exclude: moves already tried this turn (e.g. rejected by the plugin).
    lookahead: also weigh Ringo's likely reply (False = the original one-move engine)."""
    board = board_from_state(s)
    hand = s["playerHand"]
    turn = s.get("turn", {})
    opp_hand = s.get("opponentHandCount", 5)
    base, _ = score_after(board, opp_hand, lookahead)
    candidates = []

    for lm in turn.get("legalMoves", []):
        card = hand[lm["card"]]
        for tgt in lm["targets"]:
            t, r = tgt["track"], tgt["row"]
            b2 = apply_place(board, card, t, r)
            sc, note = score_after(b2, opp_hand, lookahead)
            sc -= FACE_COST.get(card["value"], 0)
            # A Queen changes nothing we model; only worth it to unblock our own caravan.
            if card["value"] == QUEEN and t < 3:
                sc += 2
            move = {"action": "place", "card": lm["card"], "track": t, "row": r}
            candidates.append((sc, move, describe(card, t, r, board), note))

    if turn.get("canDiscardCard"):
        for i, card in enumerate(hand):
            # Keep Jacks and Kings; Queens and stranded low cards are the cheapest to lose.
            keep_value = {JACK: 8, KING: 6, JOKER: 9, QUEEN: 1}.get(card["value"], 2)
            sc = base - 4 - keep_value
            candidates.append((sc, {"action": "discard", "card": i}, f"discard {card['card']}", ""))

    for t in turn.get("discardTracks", []):
        b2 = apply_discard_track(board, t)
        sc, note = score_after(b2, opp_hand, lookahead)
        sc -= 3
        candidates.append((sc, {"action": "discard_track", "track": t}, f"discard my caravan {t}", note))

    candidates = [c for c in candidates if c[1] not in exclude]
    if not candidates:
        return None, "no legal move found"
    candidates.sort(key=lambda c: c[0], reverse=True)
    sc, move, desc, note = candidates[0]
    after = totals(apply_place(board, hand[move["card"]], move["track"], move["row"])) if move["action"] == "place" \
        else (totals(apply_discard_track(board, move["track"])) if move["action"] == "discard_track" else totals(board))
    reason = f"{desc} -> mine {after[0]}/{after[1]}/{after[2]} vs npc {after[5]}/{after[4]}/{after[3]}"
    if note:
        reason += f" ({note})"
    return move, reason
