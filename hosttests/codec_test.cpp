// Host tests for mods/coop/codec.h: base64, JSON, URLs, query escaping and room codes.
#include "check.h"
#include "../mods/coop/codec.h"
using namespace remodcodec;

static std::string Q(const char* s) { return Quote(s); }
// Parse and ParseUrl fill the object they are given and online.cpp always passes a new one: so do the tests
static bool P(const std::string& text, Json& out) { out = Json(); return Parse(text, out); }
static bool U(const std::string& text, Url& out) { out = Url(); return ParseUrl(text, out); }

int main()
{
    // base64: the RFC 4648 test vectors, then every byte value and every length round-trip
    CHECK(ToB64("") == ""); CHECK(ToB64("f") == "Zg=="); CHECK(ToB64("fo") == "Zm8="); CHECK(ToB64("foo") == "Zm9v");
    CHECK(ToB64("foob") == "Zm9vYg=="); CHECK(ToB64("fooba") == "Zm9vYmE="); CHECK(ToB64("foobar") == "Zm9vYmFy");
    CHECK(FromB64("Zm9vYmFy") == "foobar"); CHECK(FromB64("Zm9vYg==") == "foob"); CHECK(FromB64("Zg") == "f");
    CHECK(FromB64("Zm9v\nYmFy") == "foobar");               // stray characters are skipped
    CHECK(FromB64("-_-_") == FromB64("+/+/"));              // URL-safe alphabet decodes like the standard one
    for (int n = 0; n < 300; n++)
    {
        std::string raw;
        for (int i = 0; i < n; i++) raw += (char)((i * 37 + n) & 0xff);
        CHECK(FromB64(ToB64(raw)) == raw);
    }
    std::string zeros(5, '\0'); CHECK(FromB64(ToB64(zeros)) == zeros);   // embedded NULs survive

    // query-string escaping
    CHECK(UrlEncode("abc-_.~XYZ019") == "abc-_.~XYZ019");
    CHECK(UrlEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
    CHECK(UrlEncode("") == "");

    // JSON: values
    Json j;
    CHECK(P("{\"a\":1,\"b\":\"x\",\"c\":[true,false,null],\"d\":{\"e\":-2.5}}", j));
    CHECK(j.kind == Json::Obj); CHECK(j["a"].integer() == 1); CHECK(j["b"].str == "x");
    CHECK(j["c"].arr.size() == 3 && j["c"].arr[0].b && !j["c"].arr[1].b && j["c"].arr[2].kind == Json::Null);
    CHECK(j["d"]["e"].num == -2.5); CHECK(j.has("a") && !j.has("zz")); CHECK(j["zz"].kind == Json::Null);
    CHECK(j["a"].text() == "1"); CHECK(j["b"].text() == "x");
    Json s;
    CHECK(P("  [ 1 , 2 ]  ", s) && s.arr.size() == 2);
    CHECK(P("[]", s) && s.arr.empty()); CHECK(P("{}", s) && s.obj.empty());
    CHECK(P("\"7\"", s) && s.integer() == 7);          // a number sent as a string (Nakama does this for some fields)
    // JSON: escapes and UTF-8
    CHECK(P("\"a\\n\\t\\\"\\\\\\/b\"", s) && s.str == "a\n\t\"\\/b");
    CHECK(P("\"\\u00e9\"", s) && s.str == "\xC3\xA9");
    CHECK(P("\"\\u20ac\"", s) && s.str == "\xE2\x82\xAC");
    CHECK(P("\"\\u0041\"", s) && s.str == "A");
    // JSON: bad input is refused, not crashed on
    CHECK(!P("", s)); CHECK(!P("{", s)); CHECK(!P("[1,", s)); CHECK(!P("{\"a\" 1}", s));
    CHECK(!P("{1:2}", s)); CHECK(!P("\"abc", s)); CHECK(!P("\"abc\\", s)); CHECK(!P("nope", s));
    std::string deep(40, '['); deep += std::string(40, ']');
    CHECK(!P(deep, s));                                // nesting deeper than 32 is refused
    std::string ok(30, '['); ok += std::string(30, ']');
    CHECK(P(ok, s));

    // Quote: what it writes, Parse reads back (including control characters and quotes)
    for (const char* t : { "", "plain", "a\"b\\c", "tab\there", "line\nbreak", "\x01\x1f", "caf\xC3\xA9" })
    {
        Json r;
        CHECK(P(Q(t), r)); CHECK(r.str == t);
    }
    CHECK(Q("a\"b") == "\"a\\\"b\"");
    CHECK(Q("\x01") == "\"\\u0001\"");

    // URLs
    Url u;
    CHECK(U("https://coop.example.com", u) && u.https && u.host == "coop.example.com" && u.port == 443 && u.prefix == "");
    CHECK(U("http://example.com", u) && !u.https && u.port == 80);
    CHECK(U("http://192.168.1.20:7350", u) && u.host == "192.168.1.20" && u.port == 7350);
    CHECK(U("https://example.com:8443/nakama/", u) && u.port == 8443 && u.prefix == "/nakama");
    CHECK(U("https://example.com//a//", u) && u.prefix == "//a");
    CHECK(U("http://[::1]/x", u) && u.host == "[::1]" && u.port == 80);    // an IPv6 literal keeps its colons
    CHECK(!U("example.com", u)); CHECK(!U("ftp://example.com", u)); CHECK(!U("https://", u));
    CHECK(!U("", u)); CHECK(!U("http://host:0", u));

    // room codes: no 0/O, 1/I/L; cleaned to upper case without anything else
    CHECK(CleanCode("ab-cd e") == "ABCDE");
    CHECK(CleanCode("o0il1") == "");                       // none of those letters or digits exist in the alphabet
    CHECK(CleanCode("ab2k9") == "AB2K9");
    CHECK(IsCode("AB2K9")); CHECK(IsCode("ab2k9"));
    CHECK(!IsCode("AB2K")); CHECK(!IsCode("AB2K99")); CHECK(!IsCode("AB2KO")); CHECK(!IsCode("AB2K1")); CHECK(!IsCode(""));
    for (const char* a = Alphabet; *a; a++) CHECK(*a != '0' && *a != 'O' && *a != '1' && *a != 'I' && *a != 'L');
    CHECK(sizeof Alphabet - 1 == 31);
    return Done("codec");
}
