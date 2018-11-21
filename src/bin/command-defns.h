#define VARIADIC (1 << 0)
#define FILTER_REQUIRED (1 << 1)
#define FILTER_UNREQUIRED (1 << 2)

typedef struct {
  const char* name;

  const char* example;
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
    "intialize a Bloom filter with n_hash_functions randomly-chosen hash functions and a bit vector of size (2**log2_bits)",
    2,
    FILTER_UNREQUIRED,
    exec_create
  },

  {
    "create-maxmem",
    "<count> <max_memory>",
    "given an expected cardinality and an approximate limit on memory consumption, initialize a Bloom filter with appropriate values of n_hash_functions and log2_bits",
    2,
    FILTER_UNREQUIRED,
    exec_create_maxmem
  },

  {
    "create-falsepos",
    "<count> <rate>",
    "given an expected cardinality and a desired false positive rate, initialize a Bloom filter with appropriate values of n_hash_functions and log2_bits",
    2,
    FILTER_UNREQUIRED,
    exec_create_falsepos
  },

  {
    "load",
    "<filename>",
    "load a previously-saved Bloom filter from disk",
    1,
    FILTER_UNREQUIRED,
    exec_load
  },

  {
    "save",
    "<filename>",
    "save the currently-loaded Bloom filter to disk",
    1,
    FILTER_REQUIRED,
    exec_save
  },

  {
    "unload",
    "",
    "unload the currently-loaded Bloom filter without persisting it to disk",
    0,
    0,
    exec_unload
  },

  {
    "insert",
    "<string> [... <string>]",
    "insert one or several string(s) into the currently-loaded Bloom filter",
    1,
    VARIADIC | FILTER_REQUIRED,
    exec_insert
  },

  {
    "insert-sha",
    "<hash> [... <hash>]",
    "given one or more SHA1 hashes, insert the string(s) represented by those hashes into the filter",
    1,
    VARIADIC | FILTER_REQUIRED,
    exec_insert_sha
  },

  {
    "query",
    "<string> [... <string>]",
    "query for the presence of one or several string(s) in the loaded filter"
  },

  {
    "falsepos",
    "",
    "empirically determine the false positive rate of the currently-loaded Bloom filter",
    0,
    FILTER_REQUIRED,
    exec_falsepos
  },

  {
    "sha",
    "<string>"
    "compute the SHA1 hash of the given string",
    0,
    exec_sha
  },

  {
    "help",
    "",
    ""
  }
};

static const size_t n_commands = sizeof(command_defns) / sizeof(command_defn_t);
