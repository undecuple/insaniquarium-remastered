// Host tests for mods/coop/wordfilter.cpp: links and a few mild words, the ways of hiding them, and words that must pass.
#include "check.h"
#include "../mods/coop/wordfilter.cpp"
using namespace wordfilter;

int main()
{
    // links
    CHECK(Bad("http://example.com")); CHECK(Bad("see www.example")); CHECK(Bad("example.com")); CHECK(Bad("Example.COM"));
    CHECK(Bad("example dot com")); CHECK(Bad("example . com")); CHECK(Bad("join foo.gg now")); CHECK(Bad("x://y"));
    CHECK(!Bad("hello. nice game")); CHECK(!Bad("version 1.2")); CHECK(!Bad("cool... right?"));
    // a few mild words, plain and disguised
    CHECK(Bad("porn")); CHECK(Bad("PORN")); CHECK(Bad("p0rn")); CHECK(Bad("p o r n")); CHECK(Bad("p.o.r.n")); CHECK(Bad("ppporn"));
    CHECK(Bad("sex")); CHECK(Bad("so sex")); CHECK(Bad("nude")); CHECK(Bad("$ex"));
    // innocent words that contain bad ones
    CHECK(!Bad("class")); CHECK(!Bad("Scunthorpe")); CHECK(!Bad("therapist")); CHECK(!Bad("torpedo")); CHECK(!Bad("assess"));
    CHECK(!Bad("")); CHECK(!Bad("nice tank! 3 fish left")); CHECK(!Bad("Insaniquarium"));
    // Clean replaces the whole text
    CHECK(Clean("porn") == "***"); CHECK(Clean("hello") == "hello");
    return Done("wordfilter");
}
