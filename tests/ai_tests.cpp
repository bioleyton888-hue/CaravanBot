// Strategy tests for the C++ engine (src/caravan_ai.h) on hand-built tables.
// Build and run with tests\run_tests.cmd.

#include <cstdio>
#include <string>

#include "../src/caravan_ai.h"

using namespace caravan_ai;

enum { H = 1, S = 2, D = 3, C = 4 };

static int g_failed = 0;

static void Check(bool ok, const char *what)
{
	printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		g_failed++;
}

// A table with the given caravans; each caravan is a list of {value, suit} number cards.
struct Table
{
	Input in = {};

	Table &Caravan(int t, std::initializer_list<std::pair<int, int>> cards)
	{
		for (auto &c : cards)
			in.board.rows[t][in.board.count[t]++] = {c.first, c.second, 0, 0, 0};
		return *this;
	}

	Table &Hand(std::initializer_list<std::pair<int, int>> cards)
	{
		for (auto &c : cards)
			in.hand.push_back({c.first, c.second, std::to_string(c.first) + "/" + std::to_string(c.second)});
		return *this;
	}

	// Legal moves as the game allows them: number cards on my caravans where they fit, face
	// cards on any existing row. Discards are allowed (not the opening).
	Input Build()
	{
		in.legal.clear();
		for (int i = 0; i < (int)in.hand.size(); i++)
		{
			LegalMove lm = {i, {}};
			const HandCard &c = in.hand[i];
			if (IsNumber(c))
			{
				for (int t = 0; t < 3; t++)
					if (Fits(in.board, t, c.value, c.suit))
						lm.targets.push_back({t, in.board.count[t]});
			}
			else
			{
				for (int t = 0; t < kTracks; t++)
					for (int r = 0; r < in.board.count[t]; r++)
						lm.targets.push_back({t, r});
			}
			if (!lm.targets.empty())
				in.legal.push_back(lm);
		}
		in.canDiscardCard = true;
		in.discardTracks.clear();
		for (int t = 0; t < 3; t++)
			if (in.board.count[t])
				in.discardTracks.push_back(t);
		in.opponentHand = 5;
		return in;
	}
};

static void TestDirectionAndSuit()
{
	Board b = {};
	b.rows[0][0] = {3, H, 0, 0, 0};
	b.rows[0][1] = {7, S, 0, 0, 0};
	b.count[0] = 2;
	Check(Direction(b, 0) == 1 && Suit(b, 0) == S, "3H then 7S: ascending, spades");

	Board q;
	ApplyPlace(b, kQueen, D, false, 0, 1, q);
	Check(Direction(q, 0) == -1 && Suit(q, 0) == D, "a Queen of diamonds on the 7: descending, diamonds");
	Check(Fits(q, 0, 5, H) && !Fits(q, 0, 9, H) && Fits(q, 0, 9, D), "after it: a 5 fits, a 9 only in diamonds");

	ApplyPlace(b, kQueen, D, false, 0, 0, q);
	Check(Direction(q, 0) == 1 && Suit(q, 0) == S, "a Queen on an earlier row changes nothing");

	Board qq;
	ApplyPlace(q, kQueen, C, false, 0, 1, qq);
	Board q2;
	ApplyPlace(qq, kQueen, H, false, 0, 1, q2);
	Check(Direction(q2, 0) == 1 && Suit(q2, 0) == H, "two Queens on the last row: back to ascending, last Queen's suit");

	Check(!Fits(b, 0, 7, S), "the same value never follows, even in the same suit");
}

// My middle caravan climbed 9D, 10C to 19: nothing goes above a 10 and I hold no clubs, so it is
// stuck. My other two caravans sit at 26, where any card busts them. The Queen is the move.
static void TestQueenUnblocksStuckCaravan()
{
	Table t;
	t.Caravan(0, {{10, H}, {9, S}, {7, D}})
		.Caravan(1, {{9, D}, {10, C}})
		.Caravan(2, {{10, S}, {7, H}, {5, D}, {4, C}})
		.Caravan(3, {{5, H}})
		.Caravan(4, {{4, S}})
		.Caravan(5, {{3, C}})
		.Hand({{2, H}, {4, D}, {kQueen, S}, {7, S}, {8, D}});
	Choice c = ChooseMove(t.Build());
	Check(c.action == kAction_Place && c.card == 2 && c.track == 1 && c.row == 1,
		("stuck caravan at 19: Queen on its 10 (got: " + c.reason + ")").c_str());
}

// My middle caravan climbs 3H, 5S and my hand has higher cards for it: a Queen would only turn it
// the wrong way, so a number card is played instead.
static void TestQueenNotWastedOnWorkingCaravan()
{
	Table t;
	t.Caravan(0, {{10, H}, {9, S}, {2, C}})
		.Caravan(1, {{3, H}, {5, S}})
		.Caravan(2, {{10, S}, {8, H}, {4, D}})
		.Caravan(3, {{5, H}})
		.Caravan(4, {{4, S}})
		.Caravan(5, {{3, C}})
		.Hand({{7, D}, {9, C}, {kQueen, S}, {10, H}, {4, D}});
	Choice c = ChooseMove(t.Build());
	Check(c.action == kAction_Place && c.track == 1 && c.card != 2,
		("working caravan at 8: a number card on it, not the Queen (got: " + c.reason + ")").c_str());
}

int main()
{
	TestDirectionAndSuit();
	TestQueenUnblocksStuckCaravan();
	TestQueenNotWastedOnWorkingCaravan();
	printf(g_failed ? "%d test(s) failed\n" : "all tests passed\n", g_failed);
	return g_failed ? 1 : 0;
}
