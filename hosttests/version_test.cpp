// Host tests for core/versions.h: the comparison the settings migration uses to decide which defaults to move on.
#include "check.h"
#include "../core/versions.h"

int main()
{
    CHECK(VersionLess("0.2.2", "0.3.0")); CHECK(!VersionLess("0.3.0", "0.2.2"));
    CHECK(!VersionLess("0.3.0", "0.3.0"));
    CHECK(VersionLess("0.3.0", "0.3.1")); CHECK(VersionLess("0.9.9", "1.0.0"));
    CHECK(VersionLess("0.10.0", "0.9.0") == false);        // numbers, not text: 10 is more than 9
    CHECK(VersionLess("0.9.0", "0.10.0"));
    CHECK(VersionLess("0.3", "0.3.1")); CHECK(!VersionLess("0.3", "0.3.0"));   // a missing part counts as 0
    CHECK(VersionLess("", "0.0.1")); CHECK(!VersionLess("", ""));
    CHECK(!VersionLess("garbage", "0.0.0"));
    return Done("version");
}
