/*
 * this is a wip program to calculate personal expenses,
 * and money spending and gain over time. I'm writing it because
 * I need a bit of helping for managing my finances, and I want a
 * flexible solution that allows me to do various different calculations
 */
#include <qhash.h>
#include <it.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct cron {
	uint64_t mask, step[3];
};

enum item_type {
	IT_ON,
	IT_LEND,
};

struct item {
	enum item_type type;
	struct cron cron;
	long long value;
};

typedef void (op_proc_t)(time_t ts, char *line);

op_proc_t op_on, op_off, op_lend, op_unlend;

struct op {
	char *name;
	op_proc_t *cb;
} op_map[] = {
	{ "ON", op_on },
	{ "OFF", op_off },
	{ "LEND", op_lend },
	{ "UNLEND", op_unlend },
};

unsigned op_hd, g_hd, itd, it_hd;

struct idm g_idm;

/* read a word */
static void
read_word(char *buf, char **input, size_t max_len)
{
	size_t ret = 0;
	char *inp = *input;

	for (; *inp && isspace(*inp); inp++, ret++);

	for (; *inp && !isspace(*inp) && ret < max_len; inp++, buf++, ret++)
		*buf = *inp;

	*buf = '\0';

	*input += ret;
}

static inline size_t
read_ts(char **line)
{
	char buf[DATE_MAX_LEN];
	read_word(buf, line, sizeof(buf));
	return sscantime(buf);
}

static inline long long
read_currency(char **line)
{
	return (long long) (strtold(*line, line) * 100.0L);
}

static inline struct cron
read_cron(char **line)
{
	struct cron cron;
	uint64_t *step = &cron.step[0];
	char *eon;
	unsigned long long nu;
	unsigned char range_start, shift_offset = 0;
	unsigned char action = 0, step_count = 0;
	memset(&cron, 0, sizeof(cron));

	(*line)++;
	for (; 1; *line += 1) {
		switch (**line) {
		case '-':
			action = 1;
			continue;
		case '/':
			action = 2;
			continue;
		case ',':
		case '*':
			action = 3;
			continue;
		case ' ':
			if (action == 3)
				*step |= 1;
			action = 0;
			step++;
			shift_offset = step_count == 0 ? 31 : 43;
			step_count++;
			if (step_count < 3)
				continue;
			break;
		default:
			if (isdigit(**line)) {
				unsigned char rnum = strtoull(*line, line, 10);
				(*line)--;

				if (action == 1) {
					for (unsigned char shift = range_start; shift < rnum; shift ++)
						cron.mask |= 1UL << (shift_offset + shift);
					action = 0;
				} else if (action == 2) {
					*step |= 1UL << rnum;
					action = 0;
					continue;
				}

				range_start = rnum;
				cron.mask |= 1UL << (shift_offset + range_start);
				continue;
			} else if (**line == ' ') {
				step++;
				if (step_count < 3)
					continue;
			} else
				continue;
		}
		break;
	}

	/* fprintf(stderr, "cron %lx %lx %lx %lx\n", cron.mask, cron.step[0], cron.step[1], cron.step[2]); */
	return cron;
}

static void
line_proc(char *line)
{
	char op_str[9], date_str[DATE_MAX_LEN];
	time_t ts;

	fprintf(stderr, "LINE: %s", line);

	if (line[0] == '#' || line[0] == '\n')
		return;

	read_word(op_str, &line, sizeof(op_str));
	op_proc_t *cb;

	if (shash_get(op_hd, &cb, op_str) < 0)
		return;

	ts = read_ts(&line);

	cb(ts, line);
}

/* This function is for 
 *
 * ON <DATE> <CRON> <VALUE> <ID>
 *
 * Which is used to turn on a recurring expense or return.
 * CRON is a month_day month week_day only cron-format string
 * (see https://crontab.guru)
 */
void
op_on(time_t ts, char *line)
{
	struct cron cron = read_cron(&line);
	long long value = read_currency(&line);
	struct item item;
	unsigned id;

	line ++;

	if (shash_get(g_hd, &id, line) < 0) {
		id = idm_new(&g_idm);
		suhash_put(g_hd, line, id);
	} else {
		fprintf(stderr, "ignored ON for an existent id\n");
		return;
	}

	it_start(itd, id, ts);
	item.type = IT_ON;
	item.cron = cron;
	item.value = value;
	uhash_put(it_hd, id, &item, sizeof(item));
}

/* This function is for 
 *
 * OFF <DATE> <ID>
 *
 * Which is used to turn off a recurring expense or return.
 */
void
op_off(time_t ts, char *line)
{
	struct item item;
	unsigned id;

	if (shash_get(g_hd, &id, line) < 0) {
		fprintf(stderr, "ignored OFF for a non-existent id\n");
		return;
	}

	uhash_get(it_hd, &item, id); // assumed success
	if (item.type != IT_ON) {
		fprintf(stderr, "ignored OFF for an incompatible id\n");
		return;
	}

	it_stop(itd, id, ts);
	uhash_del(it_hd, id);
}

/* This function is for 
 *
 * LEND <DATE> <VALUE> <ID>
 *
 * Which is used to say that we have lended some money
 */
void
op_lend(time_t ts, char *line)
{
	long long value = read_currency(&line);
	struct item item;
	unsigned id;

	if (shash_get(g_hd, &id, line) < 0) {
		id = idm_new(&g_idm);
		suhash_put(g_hd, line, id);
		item.value = value;
		item.type = IT_LEND;
	} else {
		uhash_get(it_hd, &item, id); // assumed success
		if (item.type != IT_LEND) {
			fprintf(stderr, "ignored LEND with incompatible id\n");
			return;
		}
		item.value += value;
		uhash_del(it_hd, id);
	}

	uhash_put(it_hd, id, &item, sizeof(item));
}

/* This function is for 
 *
 * UNLEND <DATE> <VALUE> <ID>
 *
 * Which is used to say that a debt has been paid to us
 */
void
op_unlend(time_t ts, char *line)
{
	long long value = read_currency(&line);
	struct item item;
	unsigned id;

	if (shash_get(g_hd, &id, line) < 0) {
		fprintf(stderr, "ignored UNLEND with non-existent id\n");
		return;
	}

	uhash_get(it_hd, &item, id); // assumed success

	if (item.type != IT_LEND) {
		fprintf(stderr, "ignored UNLEND with incompatible id\n");
		return;
	}

	item.value -= value;
	uhash_del(it_hd, id);
	uhash_put(it_hd, id, &item, sizeof(item));
}

int main() {
	char *line = NULL;
	ssize_t linelen;
	size_t linesize;

	g_idm = idm_init();
	op_hd = hash_init();
	g_hd = hash_init();
	it_hd = hash_init();
	itd = it_init(NULL);

	for (int i = 0; i < 2; i++)
		shash_put(op_hd, op_map[i].name,
				&op_map[i].cb, sizeof(op_map[i].cb));

	while ((linelen = getline(&line, &linesize, stdin)) >= 0) {
		line_proc(line);
		char *eol = strchr(line, '\n');
		*eol = '\0';
	}

	free(line);
	return EXIT_SUCCESS;
}
