#define VARIADIC (1 << 0)
#define FILTER_REQUIRED (1 << 1)
#define FILTER_UNREQUIRED (1 << 2)

typedef struct {
  const char* name;

  const char* usage;
  const char* description;

  size_t min_arity;
  size_t flags;

  void (*exec)(executor_t* ex);
} command_defn_t;

static void exec_create(executor_t* ex);
static void exec_create_maxmem(executor_t* ex);
static void exec_create_falsepos(executor_t* ex);
static void exec_load(executor_t* ex);
static void exec_save(executor_t* ex);
static void exec_unload(executor_t* ex);
static void exec_insert(executor_t* ex);
static void exec_insert_sha(executor_t* ex);
static void exec_query(executor_t* ex);
static void exec_query_sha(executor_t* ex);
static void exec_falsepos(executor_t* ex);
static void exec_sha(executor_t* ex);
static void exec_help(executor_t* ex);

static const command_defn_t command_defns[] = {
  {
    "create",
    "<n_hash_functions> <log2_bits>",
    "Intialize a Bloom filter with n_hash_functions randomly-chosen hash functions and a bit vector of size (2**log2_bits).",
    2,
    FILTER_UNREQUIRED,
    exec_create
  },

  {
    "create-maxmem",
    "<count> <max_memory>",
    "Intialize a Bloom filter with an approximate memory limit, given the expected cardinality of the set.",
    2,
    FILTER_UNREQUIRED,
    exec_create_maxmem
  },

  {
    "create-falsepos",
    "<count> <rate>",
    "Initialize a Bloom filter with an approximate goal false positive rate, given the expected cardinality of the set.",
    2,
    FILTER_UNREQUIRED,
    exec_create_falsepos
  },

  {
    "load",
    "<filename>",
    "Load a previously-saved Bloom filter from disk.",
    1,
    FILTER_UNREQUIRED,
    exec_load
  },

  {
    "save",
    "<filename>",
    "Save the currently-loaded Bloom filter to disk.",
    1,
    FILTER_REQUIRED,
    exec_save
  },

  {
    "unload",
    "",
    "Unload the currently-loaded Bloom filter without persisting it to disk.",
    0,
    0,
    exec_unload
  },

  {
    "insert",
    "<string> [... <string>]",
    "Insert one or several string(s) into the Bloom filter.",
    1,
    VARIADIC | FILTER_REQUIRED,
    exec_insert
  },

  {
    "insert-sha",
    "<hash> [... <hash>]",
    "Insert one or several string(s), encoded as SHA1 hashes, into the Bloom filter.",
    1,
    VARIADIC | FILTER_REQUIRED,
    exec_insert_sha
  },

  {
    "query",
    "<string> [... <string>]",
    "Query for the presence of one or several string(s) in the Bloom filter.",
    1,
    VARIADIC | FILTER_REQUIRED,
    exec_query_sha
  },

  {
    "query-sha",
    "<hash> [... <hash>]",
    "Query for the presence of one or several string(s), encoded as SHA1 hashes, in the Bloom filter.",
    1,
    VARIADIC | FILTER_REQUIRED,
    exec_query
  },

  {
    "falsepos",
    "",
    "Empirically test the false positive rate of the currently-loaded Bloom filter by repeated random trials.",
    0,
    FILTER_REQUIRED,
    exec_falsepos
  },

  {
    "sha",
    "<string>",
    "Compute the SHA1 hash of the given string.",
    0,
    0,
    exec_sha
  },

  {
    "help",
    "[command]",
    "List available commands, or show detailed documentation for one command.",
    0,
    VARIADIC,
    exec_help
  }
};

static const size_t n_commands = sizeof(command_defns) / sizeof(command_defn_t);
