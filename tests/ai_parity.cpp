// Compares the C++ engine (src/caravan_ai.h) with the Python one (tools/caravan_ai.py) on every
// saved turn and lists the turns where they pick different moves. Since 1.0.2 only the C++
// engine knows directions and Queens, so differences are expected: review them, they are
// exactly what the new logic changed. Build and run with tests\run_parity.cmd.
// Usage: ai_parity <file written by tools/export_parity.py>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/caravan_ai.h"

using namespace caravan_ai;

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("usage: ai_parity <parity file>\n");
		return 2;
	}
	std::ifstream file(argv[1]);
	std::string line;
	int total = 0, same = 0;
	double slowestMs = 0;
	while (std::getline(file, line))
	{
		if (line.empty())
			continue;
		std::istringstream in(line);
		Input input = {};
		int canDiscard = 0, n = 0;
		in >> input.opponentHand >> canDiscard >> n;
		input.canDiscardCard = canDiscard != 0;
		std::string hand;
		for (int i = 0; i < n; i++)
		{
			static const char *kValues[] = {"?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "?", "J", "Q", "K", "JOKER"};
			static const char kSuits[] = {'?', 'H', 'S', 'D', 'C', '*'};
			HandCard c;
			in >> c.value >> c.suit;
			c.code = c.value == 15 ? "JOKER" : std::string(kValues[c.value]) + kSuits[c.suit];
			input.hand.push_back(c);
			hand += (i ? " " : "") + c.code;
		}
		for (int t = 0; t < kTracks; t++)
		{
			in >> input.board.count[t];
			for (int r = 0; r < input.board.count[t]; r++)
			{
				Row &row = input.board.rows[t][r];
				in >> row.value >> row.suit >> row.kings >> row.queens >> row.queenSuit;
			}
		}
		in >> n;
		for (int i = 0; i < n; i++)
		{
			LegalMove lm;
			int targets = 0;
			in >> lm.card >> targets;
			for (int k = 0; k < targets; k++)
			{
				Target tg;
				in >> tg.track >> tg.row;
				lm.targets.push_back(tg);
			}
			input.legal.push_back(lm);
		}
		in >> n;
		for (int i = 0; i < n; i++)
		{
			int t;
			in >> t;
			input.discardTracks.push_back(t);
		}
		int action, card, track, row;
		in >> action >> card >> track >> row;

		// ai_parity <file> <line>: print every candidate's score parts for that line.
		if (argc > 2 && atoi(argv[2]) == total + 1)
		{
			int tot[kTracks];
			Totals(input.board, tot);
			printf("totals mine %d/%d/%d npc %d/%d/%d, dirs", tot[0], tot[1], tot[2], tot[5], tot[4], tot[3]);
			for (int t = 0; t < kTracks; t++)
				printf(" %d%s", Direction(input.board, t), input.board.count[t] ? "" : "(empty)");
			printf("\n");
			const char *note = "";
			for (const LegalMove &lm : input.legal)
				for (const Target &tg : lm.targets)
				{
					const HandCard &card = input.hand[lm.card];
					Board b2;
					if (!ApplyPlace(input.board, card.value, card.suit, IsNumber(card), tg.track, tg.row, b2))
						continue;
					double sa = ScoreAfter(b2, input.opponentHand, &note);
					int g = PlayableCaravans(b2, input.hand);
					printf("  %-4s t%d r%d: after %.1f cost %d playable %d -> %.1f\n", card.code.c_str(), tg.track, tg.row,
						sa, FaceCost(card.value), g, sa - FaceCost(card.value) + kPlayableWeight * g);
				}
			int g0 = PlayableCaravans(input.board, input.hand);
			printf("  (playable before the move: %d)\n", g0);
		}

		auto start = std::chrono::steady_clock::now();
		Choice c = ChooseMove(input);
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		if (ms > slowestMs)
			slowestMs = ms;

		total++;
		bool match = c.action == action &&
			(action == kAction_DiscardTrack || c.card == card) &&
			(action != kAction_Place || (c.track == track && c.row == row)) &&
			(action != kAction_DiscardTrack || c.track == track);
		if (match)
			same++;
		else
			printf("line %d hand [%s]: python %d card=%d track=%d row=%d | c++ %s\n",
				total, hand.c_str(), action, card, track, row, c.reason.c_str());
	}
	printf("%d of %d turns pick the same move; slowest decision %.1f ms\n", same, total, slowestMs);
	return total > 0 ? 0 : 1;
}
