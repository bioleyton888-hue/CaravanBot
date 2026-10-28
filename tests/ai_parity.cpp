// Checks that the C++ engine (src/caravan_ai.h) picks the same move as tools/caravan_ai.py on
// every saved turn. Build and run with tests\run_parity.cmd.
// Usage: ai_parity <file written by tools/export_parity.py>

#include <chrono>
#include <cstdio>
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
		for (int i = 0; i < n; i++)
		{
			HandCard c;
			in >> c.value >> c.suit;
			input.hand.push_back(c);
		}
		for (int t = 0; t < kTracks; t++)
		{
			in >> input.board.count[t];
			for (int r = 0; r < input.board.count[t]; r++)
				in >> input.board.rows[t][r].value >> input.board.rows[t][r].suit >> input.board.rows[t][r].kings;
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
			printf("line %d: python %d card=%d track=%d row=%d, c++ %d card=%d track=%d row=%d (%s)\n",
				total, action, card, track, row, c.action, c.card, c.track, c.row, c.reason.c_str());
	}
	printf("%d of %d turns pick the same move; slowest decision %.1f ms\n", same, total, slowestMs);
	return same == total && total > 0 ? 0 : 1;
}
