// Caravan strategy engine for CaravanBot: a C++ port of tools/caravan_ai.py.
//
// ChooseMove() takes the table on an awaited turn (board, hand, the engine's legal moves) and
// returns the move to play. It is a one-move lookahead: every legal move and discard is
// simulated and the resulting table is scored, blended with Ringo's most damaging likely reply.
// A caravan's total is the sum over its rows of number value x 2 per King on that row.
//
// The Python file stays the reference: tests/ai_parity.cpp checks that both pick the same move
// on every saved turn. Keep the two in step when the strategy changes.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace caravan_ai
{

enum
{
	kKing = 14,
	kQueen = 13,
	kJack = 12,
	kJoker = 15,
	kSoldMin = 21,
	kSoldMax = 26,
	kTracks = 6,
	kRows = 7,
};

struct HandCard
{
	int value;  // 1-10, 12 J, 13 Q, 14 K, 15 Joker
	int suit;   // 1 hearts, 2 spades, 3 diamonds, 4 clubs, 5 joker, 0 unknown (Ringo's simulated cards)
	std::string code;
};

// One row of a caravan: its number card and how many Kings sit on it (Queens and Jokers change
// no total, so they are not kept).
struct Row
{
	int value;
	int suit;
	int kings;
};

struct Board
{
	Row rows[kTracks][kRows];
	int count[kTracks];
};

struct Target
{
	int track, row;
};

struct LegalMove
{
	int card;
	std::vector<Target> targets;
};

struct Input
{
	Board board;
	std::vector<HandCard> hand;
	std::vector<LegalMove> legal;
	bool canDiscardCard;
	std::vector<int> discardTracks;
	int opponentHand;
};

enum Action
{
	kAction_None,
	kAction_Place,
	kAction_Discard,
	kAction_DiscardTrack,
};

struct Choice
{
	Action action = kAction_None;
	int card = -1, track = -1, row = -1;
	std::string reason;
};

inline bool IsNumber(const HandCard &c) { return c.value >= 1 && c.value <= 10 && c.suit != 5; }

inline int RowValue(const Row &r) { return r.value << r.kings; }

inline void Totals(const Board &b, int out[kTracks])
{
	for (int t = 0; t < kTracks; t++)
	{
		out[t] = 0;
		for (int r = 0; r < b.count[t]; r++)
			out[t] += RowValue(b.rows[t][r]);
	}
}

inline bool Sold(int v) { return v >= kSoldMin && v <= kSoldMax; }

// +1 I win the pair, -1 Ringo wins it, 0 undecided.
inline int PairWinner(int mine, int theirs)
{
	if (Sold(mine) && (!Sold(theirs) || mine > theirs))
		return 1;
	if (Sold(theirs) && (!Sold(mine) || theirs > mine))
		return -1;
	return 0;
}

// ---- Simulation ---------------------------------------------------------------------------

// Returns false when the target row does not exist (nothing to simulate).
inline bool ApplyPlace(const Board &in, int value, int suit, bool number, int track, int row, Board &b)
{
	b = in;
	if (number)
	{
		if (b.count[track] >= kRows)
			return false;
		b.rows[track][b.count[track]++] = {value, suit, 0};
		return true;
	}
	if (row < 0 || row >= b.count[track])
		return false;
	if (value == kJack)
	{
		for (int r = row; r + 1 < b.count[track]; r++)
			b.rows[track][r] = b.rows[track][r + 1];
		b.count[track]--;
	}
	else if (value == kJoker)
	{
		Row target = b.rows[track][row];
		for (int t = 0; t < kTracks; t++)
		{
			int kept = 0;
			for (int r = 0; r < b.count[t]; r++)
			{
				const Row &cell = b.rows[t][r];
				bool isTarget = t == track && r == row;
				bool hit = target.value == 1 ? cell.suit == target.suit : cell.value == target.value;
				if (hit && !isTarget)
					continue;
				b.rows[t][kept++] = cell;
			}
			b.count[t] = kept;
		}
	}
	else if (value == kKing)
	{
		b.rows[track][row].kings++;
	}
	// A Queen changes direction/suit only.
	return true;
}

inline Board ApplyDiscardTrack(const Board &in, int track)
{
	Board b = in;
	b.count[track] = 0;
	return b;
}

// ---- Scoring ------------------------------------------------------------------------------

// How good a total is for its owner, ignoring the opponent.
inline double CaravanScore(int v)
{
	if (v > kSoldMax)
		// Bad, but repairable (a Jack on the overloaded row): not much worse than an empty caravan.
		return -10 - (v - kSoldMax) * 0.3;
	if (v >= kSoldMin)
		return 30 + (v - kSoldMin) * 1.5;
	// Progress toward 21; overshooting is worse than undershooting, so reward steadily.
	return v * 1.0;
}

// Score a table from my point of view. terminal: the bonus/penalty when every pair is decided
// (the game ends). note: "wins the game", "would end the game as a loss" or empty.
inline double Evaluate(const Board &b, double terminal, const char **note = nullptr)
{
	int vals[kTracks];
	Totals(b, vals);
	double score = 0.0;
	int me = 0, npc = 0, undecided = 0;
	for (int t = 0; t < 3; t++)
	{
		int mine = vals[t], theirs = vals[5 - t];
		score += CaravanScore(mine) - CaravanScore(theirs);
		int w = PairWinner(mine, theirs);
		if (w > 0)
		{
			me++;
			score += 25;
		}
		else if (w < 0)
		{
			npc++;
			score -= 25;
		}
		else
			undecided++;
	}
	const char *n = "";
	if (undecided == 0)  // every pair decided: the game ends right now
	{
		if (me >= 2)
		{
			score += terminal;
			n = "wins the game";
		}
		else
		{
			score -= terminal;
			n = "would end the game as a loss";
		}
	}
	if (note)
		*note = n;
	return score;
}

inline int FaceCost(int value)
{
	switch (value)
	{
	case kJack: return 6;
	case kKing: return 3;
	case kQueen: return 1;
	case kJoker: return 7;
	default: return 0;
	}
}

// ---- Ringo's reply ------------------------------------------------------------------------
//
// We never see Ringo's hand, only its size. Each card kind is assumed equally common in its deck
// (4 of each value per 54 cards, 2 Jokers), so with h cards it holds a given kind with
// probability 1 - (1 - q)^h. Ringo is assumed to play the most damaging kind it holds; the
// expected score is taken over which kinds it holds. Queens and discards are treated as "no harm".

const double kReplyTerminal = 150;  // a game Ringo could end next turn: a big swing, but not worth
                                    // wrecking my own winning caravans to avoid
const double kReplyWeight = 0.5;    // how much Ringo's likely reply counts against the table right after my move

// Whether a number card of value v may extend a caravan, by direction only (suit unknown).
inline bool NumberFits(const Board &b, int t, int v)
{
	int n = b.count[t];
	if (n == 0)
		return true;
	int last = b.rows[t][n - 1].value;
	if (v == last)
		return false;
	if (n < 2)
		return true;
	int prev = b.rows[t][n - 2].value;
	return last > prev ? v > last : v < last;
}

// Ringo's best (lowest for me) score with one card of this kind; false if it has no play.
inline bool BestReply(const Board &board, int kind, double &best)
{
	bool any = false;
	Board b;
	if (kind <= 10)
	{
		for (int t = 3; t < 6; t++)
			if (board.count[t] < kRows && NumberFits(board, t, kind) && ApplyPlace(board, kind, 0, true, t, board.count[t], b))
			{
				double sc = Evaluate(b, kReplyTerminal);
				if (!any || sc < best)
					best = sc;
				any = true;
			}
		return any;
	}
	for (int t = 0; t < kTracks; t++)
		for (int r = 0; r < board.count[t]; r++)
			if (ApplyPlace(board, kind, 0, false, t, r, b))
			{
				double sc = Evaluate(b, kReplyTerminal);
				if (!any || sc < best)
					best = sc;
				any = true;
			}
	return any;
}

// My expected score after Ringo's best reply with the cards it is likely to hold.
inline double ExpectedAfterReply(const Board &board, int opponentHand)
{
	static const int kKinds[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, kJack, kKing, kJoker};
	double standstill = Evaluate(board, kReplyTerminal);
	std::vector<std::pair<double, double>> outcomes;  // (score, probability Ringo holds that kind)
	for (int kind : kKinds)
	{
		double q = kind == kJoker ? 2.0 / 54 : 4.0 / 54;
		double best = standstill;
		BestReply(board, kind, best);
		if (best < standstill)
			outcomes.push_back({best, 1 - std::pow(1 - q, (double)(opponentHand > 1 ? opponentHand : 1))});
	}
	// Ringo plays its most damaging held kind: walk the kinds from worst for me upward.
	std::sort(outcomes.begin(), outcomes.end());
	double expected = 0.0, noneBetter = 1.0;
	for (auto &o : outcomes)
	{
		expected += noneBetter * o.second * o.first;
		noneBetter *= 1 - o.second;
	}
	return expected + noneBetter * standstill;
}

// Score of the table after my move: final if the game ends, else blended with the expected
// score after Ringo's likely reply (pure expectation made the bot discard sold caravans for fear).
inline double ScoreAfter(const Board &b, int opponentHand, const char **note)
{
	double sc = Evaluate(b, 1000, note);
	if (**note)
		return sc;
	return (1 - kReplyWeight) * sc + kReplyWeight * ExpectedAfterReply(b, opponentHand);
}

inline std::string Describe(const HandCard &card, int track, int row, const Board &before)
{
	std::string owner = track < 3 ? "my" : "NPC's";
	if (IsNumber(card))
		return card.code + " on " + owner + " caravan " + std::to_string(track);
	static const char *kValues[] = {"?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
	static const char kSuits[] = {'?', 'H', 'S', 'D', 'C', '*'};
	std::string target = "?";
	if (row < before.count[track])
	{
		const Row &r = before.rows[track][row];
		target = std::string(r.value >= 1 && r.value <= 10 ? kValues[r.value] : "?") + (r.suit >= 0 && r.suit <= 5 ? kSuits[r.suit] : '?');
	}
	return card.code + " on " + owner + " " + target + " (caravan " + std::to_string(track) + ", row " + std::to_string(row) + ")";
}

inline Choice ChooseMove(const Input &in)
{
	const Board &board = in.board;
	const char *note = "";
	double base = ScoreAfter(board, in.opponentHand, &note);

	Choice best;
	double bestScore = 0;
	Board bestBoard = board;
	std::string bestDesc, bestNote;
	auto consider = [&](double sc, Choice c, const Board &after, const std::string &desc, const char *n) {
		// The first of equal scores wins, as in the Python engine's stable sort.
		if (best.action == kAction_None || sc > bestScore)
		{
			best = c;
			bestScore = sc;
			bestBoard = after;
			bestDesc = desc;
			bestNote = n;
		}
	};

	Board b2;
	for (const LegalMove &lm : in.legal)
	{
		if (lm.card < 0 || lm.card >= (int)in.hand.size())
			continue;
		const HandCard &card = in.hand[lm.card];
		for (const Target &tg : lm.targets)
		{
			if (!ApplyPlace(board, card.value, card.suit, IsNumber(card), tg.track, tg.row, b2))
				continue;
			double sc = ScoreAfter(b2, in.opponentHand, &note);
			sc -= FaceCost(card.value);
			// A Queen changes nothing we model; only worth it to unblock our own caravan.
			if (card.value == kQueen && tg.track < 3)
				sc += 2;
			Choice c;
			c.action = kAction_Place;
			c.card = lm.card;
			c.track = tg.track;
			c.row = tg.row;
			consider(sc, c, b2, Describe(card, tg.track, tg.row, board), note);
		}
	}

	if (in.canDiscardCard)
		for (int i = 0; i < (int)in.hand.size(); i++)
		{
			// Keep Jacks and Kings; Queens and stranded low cards are the cheapest to lose.
			int v = in.hand[i].value;
			int keep = v == kJack ? 8 : v == kKing ? 6 : v == kJoker ? 9 : v == kQueen ? 1 : 2;
			Choice c;
			c.action = kAction_Discard;
			c.card = i;
			consider(base - 4 - keep, c, board, "discard " + in.hand[i].code, "");
		}

	for (int t : in.discardTracks)
	{
		b2 = ApplyDiscardTrack(board, t);
		double sc = ScoreAfter(b2, in.opponentHand, &note) - 3;
		Choice c;
		c.action = kAction_DiscardTrack;
		c.track = t;
		consider(sc, c, b2, "discard my caravan " + std::to_string(t), note);
	}

	if (best.action == kAction_None)
	{
		best.reason = "no legal move found";
		return best;
	}
	int after[kTracks];
	Totals(bestBoard, after);
	char buf[96];
	sprintf_s(buf, " -> mine %d/%d/%d vs npc %d/%d/%d", after[0], after[1], after[2], after[5], after[4], after[3]);
	best.reason = bestDesc + buf;
	if (!bestNote.empty())
		best.reason += " (" + bestNote + ")";
	return best;
}

}  // namespace caravan_ai
