/*
HyperDrive Companion
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/
#include "test.hpp"

namespace test {

std::vector<Case> &cases()
{
	static std::vector<Case> all;
	return all;
}

int failures = 0;

void fail(const char *file, int line, const std::string &message)
{
	failures++;
	std::cerr << "  FAILED " << file << ":" << line << ": " << message << "\n";
}

} // namespace test

int main()
{
	int failedCases = 0;
	for (const test::Case &c : test::cases()) {
		int before = test::failures;
		c.run();
		bool ok = test::failures == before;
		if (!ok)
			failedCases++;
		std::cout << (ok ? "ok   " : "FAIL ") << c.name << "\n";
	}
	std::cout << test::cases().size() - static_cast<size_t>(failedCases) << "/" << test::cases().size()
		  << " passed\n";
	return failedCases == 0 ? 0 : 1;
}
