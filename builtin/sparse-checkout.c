#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout [init|list]"),
	NULL
};

static char *get_sparse_checkout_filename(void)
{
	return git_pathdup("info/sparse-checkout");
}

static void write_patterns_to_file(FILE *fp, struct pattern_list *pl)
{
	int i;

	for (i = 0; i < pl->nr; i++) {
		struct path_pattern *p = pl->patterns[i];

		if (p->flags & PATTERN_FLAG_NEGATIVE)
			fprintf(fp, "!");

		fprintf(fp, "%s", p->pattern);

		if (p->flags & PATTERN_FLAG_MUSTBEDIR)
			fprintf(fp, "/");

		fprintf(fp, "\n");
	}
}

static int sparse_checkout_list(int argc, const char **argv)
{
	struct pattern_list pl;
	char *sparse_filename;
	int res;

	memset(&pl, 0, sizeof(pl));

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL);
	free(sparse_filename);

	if (res < 0) {
		warning(_("this worktree is not sparse (sparse-checkout file may not exist)"));
		return 0;
	}

	write_patterns_to_file(stdout, &pl);
	clear_pattern_list(&pl);

	return 0;
}

static int update_working_directory(void)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	int result = 0;
	argv_array_pushl(&argv, "read-tree", "-m", "-u", "HEAD", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to update index with new sparse-checkout paths"));
		result = 1;
	}

	argv_array_clear(&argv);
	return result;
}

enum sparse_checkout_mode {
	MODE_NONE = 0,
	MODE_FULL = 1,
};

static int sc_set_config(enum sparse_checkout_mode mode)
{
	struct argv_array argv = ARGV_ARRAY_INIT;

	if (git_config_set_gently("extensions.worktreeConfig", "true")) {
		error(_("failed to set extensions.worktreeConfig setting"));
		return 1;
	}

	argv_array_pushl(&argv, "config", "--worktree", "core.sparseCheckout", NULL);

	if (mode)
		argv_array_pushl(&argv, "true", NULL);
	else
		argv_array_pushl(&argv, "false", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to enable core.sparseCheckout"));
		return 1;
	}

	return 0;
}

static int sparse_checkout_init(int argc, const char **argv)
{
	struct pattern_list pl;
	char *sparse_filename;
	FILE *fp;
	int res;
	struct object_id oid;

	if (sc_set_config(MODE_FULL))
		return 1;

	memset(&pl, 0, sizeof(pl));

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL);

	/* If we already have a sparse-checkout file, use it. */
	if (res >= 0) {
		free(sparse_filename);
		goto reset_dir;
	}

	/* initial mode: all blobs at root */
	fp = fopen(sparse_filename, "w");
	free(sparse_filename);
	fprintf(fp, "/*\n!/*/\n");
	fclose(fp);

	if (get_oid("HEAD", &oid)) {
		/* assume we are in a fresh repo */
		return 0;
	}

reset_dir:
	return update_working_directory();
}

int cmd_sparse_checkout(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_sparse_checkout_options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_sparse_checkout_usage,
				   builtin_sparse_checkout_options);

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_options,
			     builtin_sparse_checkout_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	git_config(git_default_config, NULL);

	if (argc > 0) {
		if (!strcmp(argv[0], "list"))
			return sparse_checkout_list(argc, argv);
		if (!strcmp(argv[0], "init"))
			return sparse_checkout_init(argc, argv);
	}

	usage_with_options(builtin_sparse_checkout_usage,
			   builtin_sparse_checkout_options);
}
