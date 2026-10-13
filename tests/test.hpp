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
#pragma once

// A minimal test runner: TEST(name) { CHECK(...); CHECK_EQ(a, b); }

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
	const char *name;
	std::function<void()> run;
};

std::vector<Case> &cases();
extern int failures;

struct Register {
	Register(const char *name, std::function<void()> run) { cases().push_back({name, std::move(run)}); }
};

void fail(const char *file, int line, const std::string &message);

template<typename A, typename B>
void checkEq(const A &a, const B &b, const char *as, const char *bs, const char *file, int line)
{
	if (!(a == b)) {
		std::ostringstream ss;
		ss << as << " == " << bs << "\n      got: " << a << "\n expected: " << b;
		fail(file, line, ss.str());
	}
}

} // namespace test

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST(name)                                                                      \
	static void TEST_CONCAT(test_, name)();                                         \
	static test::Register TEST_CONCAT(register_, name)(#name, TEST_CONCAT(test_, name)); \
	static void TEST_CONCAT(test_, name)()
#define CHECK(cond)                                         \
	do {                                                \
		if (!(cond))                                \
			test::fail(__FILE__, __LINE__, #cond); \
	} while (0)
#define CHECK_EQ(a, b) test::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
