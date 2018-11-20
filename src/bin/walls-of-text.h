static const char* usage =
  "OVERVIEW: command-line tool for building and querying Bloom filters\n\n"
  "USAGE:\n\n"
  "  %s                start an interactive session\n"
  "  %s -              read and run a script from the standard input\n"
  "  %s <filename>     read and run the script specified by filename\n"
  "  %s -c <commands>  run given sequence of commands from the second argument\n\n"
  "Run `help` in an interactive session to learn about the hibp-bloom scripting language\n";

static const char* banner =
  "=====================================================================\n"
  "hibp-bloom: command-line tool for building and querying Bloom filters\n"
  "Built at " __TIME__ " on " __DATE__ "\n"
  "Try `help` to learn about the hibp-bloom scripting language\n"
  "=====================================================================\n";
