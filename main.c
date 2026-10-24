#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <curses.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_WIDTH 14
#define MAX_EDIT_LEN 4096

typedef struct {
    char ***cells;
    int *cell_counts;
    int row_count;
    int max_cols;
    bool dirty;
    char path[512];
} CSVTable;

static void table_init(CSVTable *table) {
    table->cells = NULL;
    table->cell_counts = NULL;
    table->row_count = 0;
    table->max_cols = 0;
    table->dirty = false;
    table->path[0] = '\0';
}

static void free_cell_row(char **row, int count) {
    if (row == NULL) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(row[i]);
    }
    free(row);
}

static void table_free(CSVTable *table) {
    if (table == NULL) {
        return;
    }

    for (int r = 0; r < table->row_count; r++) {
        if (table->cells[r] != NULL) {
            free_cell_row(table->cells[r], table->cell_counts[r]);
        }
    }

    free(table->cells);
    free(table->cell_counts);
    table_init(table);
}

static void append_blank_row(CSVTable *table) {
    table->cells = realloc(table->cells, sizeof(char **) * (table->row_count + 1));
    table->cell_counts = realloc(table->cell_counts, sizeof(int) * (table->row_count + 1));
    table->cells[table->row_count] = NULL;
    table->cell_counts[table->row_count] = 0;
    table->row_count++;
}

static void ensure_row(CSVTable *table, int row) {
    while (row >= table->row_count) {
        append_blank_row(table);
    }
}

static void set_cell(CSVTable *table, int row, int col, const char *value) {
    if (table == NULL || row < 0 || col < 0) {
        return;
    }

    ensure_row(table, row);

    if (table->cells[row] == NULL) {
        table->cells[row] = calloc(1, sizeof(char *));
    }

    if (col >= table->cell_counts[row]) {
        int old_count = table->cell_counts[row];
        int new_count = col + 1;
        table->cells[row] = realloc(table->cells[row], sizeof(char *) * new_count);
        for (int i = old_count; i < new_count; i++) {
            table->cells[row][i] = strdup("");
        }
        table->cell_counts[row] = new_count;
    }

    free(table->cells[row][col]);
    table->cells[row][col] = strdup(value != NULL ? value : "");

    if (col + 1 > table->max_cols) {
        table->max_cols = col + 1;
    }
    table->dirty = true;
}

static char *cell_value(const CSVTable *table, int row, int col) {
    if (table == NULL || row < 0 || col < 0 || row >= table->row_count || col >= table->max_cols) {
        return (char *)"";
    }

    if (table->cells[row] == NULL || col >= table->cell_counts[row]) {
        return (char *)"";
    }

    return table->cells[row][col] != NULL ? table->cells[row][col] : (char *)"";
}

static void delete_row(CSVTable *table, int row) {
    if (table == NULL || row < 0 || row >= table->row_count) {
        return;
    }

    if (table->cells[row] != NULL) {
        free_cell_row(table->cells[row], table->cell_counts[row]);
    }

    for (int i = row; i + 1 < table->row_count; i++) {
        table->cells[i] = table->cells[i + 1];
        table->cell_counts[i] = table->cell_counts[i + 1];
    }

    table->row_count--;
    table->max_cols = 0;
    for (int r = 0; r < table->row_count; r++) {
        int current_max = table->cell_counts[r];
        if (current_max > table->max_cols) {
            table->max_cols = current_max;
        }
    }
    table->dirty = true;
}

static void add_column_to_row(CSVTable *table, int row) {
    if (table == NULL || row < 0 || row >= table->row_count) {
        return;
    }

    ensure_row(table, row);
    int col = table->cell_counts[row];
    table->cells[row] = realloc(table->cells[row], sizeof(char *) * (col + 1));
    table->cells[row][col] = strdup("");
    table->cell_counts[row] = col + 1;
    table->max_cols = (table->max_cols > col + 1) ? table->max_cols : (col + 1);
    table->dirty = true;
}

static void trim_newline(char *text) {
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[--len] = '\0';
    }
}

static char *column_label(int index) {
    static char buffer[16];
    int value = index;
    int len = 0;
    char temp[16];

    do {
        int rem = value % 26;
        temp[len++] = (char)('A' + rem);
        value = value / 26 - 1;
    } while (value >= 0);

    buffer[len] = '\0';
    for (int i = 0; i < len; i++) {
        buffer[i] = temp[len - 1 - i];
    }

    return buffer;
}

static int parse_csv_row(const char *line, char ***out_row) {
    size_t capacity = 8;
    int count = 0;
    char **row = calloc(capacity, sizeof(char *));
    char *buffer = calloc(strlen(line) + 1, sizeof(char));
    size_t buffer_index = 0;
    bool in_quotes = false;

    for (size_t i = 0; line[i] != '\0'; i++) {
        char c = line[i];

        if (c == '"') {
            if (in_quotes && line[i + 1] == '"') {
                buffer[buffer_index++] = '"';
                i++;
            } else {
                in_quotes = !in_quotes;
            }
            continue;
        }

        if (c == ',' && !in_quotes) {
            buffer[buffer_index] = '\0';
            row[count++] = strdup(buffer);
            if (count >= (int)capacity) {
                capacity *= 2;
                row = realloc(row, sizeof(char *) * capacity);
            }
            buffer_index = 0;
            buffer[0] = '\0';
            continue;
        }

        buffer[buffer_index++] = c;
    }

    buffer[buffer_index] = '\0';
    row[count++] = strdup(buffer);

    free(buffer);
    *out_row = row;
    return count;
}

static char *csv_escape(const char *value) {
    if (value == NULL) {
        value = "";
    }

    bool needs_quotes = false;
    size_t len = strlen(value);
    for (size_t i = 0; i < len; i++) {
        if (value[i] == ',' || value[i] == '"' || value[i] == '\n' || value[i] == '\r') {
            needs_quotes = true;
            break;
        }
    }

    if (!needs_quotes) {
        return strdup(value);
    }

    size_t extra = 0;
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '"') {
            extra++;
        }
    }

    char *escaped = malloc(len + extra * 2 + 3);
    size_t out_index = 0;
    escaped[out_index++] = '"';
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '"') {
            escaped[out_index++] = '"';
            escaped[out_index++] = '"';
        } else {
            escaped[out_index++] = value[i];
        }
    }
    escaped[out_index++] = '"';
    escaped[out_index] = '\0';
    return escaped;
}

static bool load_csv_file(CSVTable *table, const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        strncpy(table->path, path, sizeof(table->path) - 1);
        table->path[sizeof(table->path) - 1] = '\0';
        append_blank_row(table);
        table->cells[0] = calloc(1, sizeof(char *));
        table->cells[0][0] = strdup("");
        table->cell_counts[0] = 1;
        table->max_cols = 1;
        return true;
    }

    char *line = NULL;
    size_t line_size = 0;
    ssize_t read;

    while ((read = getline(&line, &line_size, file)) != -1) {
        (void)read;
        trim_newline(line);

        char **row_values = NULL;
        int row_len = parse_csv_row(line, &row_values);

        append_blank_row(table);
        table->cells[table->row_count - 1] = row_values;
        table->cell_counts[table->row_count - 1] = row_len;
        if (row_len > table->max_cols) {
            table->max_cols = row_len;
        }
    }

    free(line);
    fclose(file);

    if (table->row_count == 0) {
        append_blank_row(table);
        table->cells[0] = calloc(1, sizeof(char *));
        table->cells[0][0] = strdup("");
        table->cell_counts[0] = 1;
        table->max_cols = 1;
    }

    strncpy(table->path, path, sizeof(table->path) - 1);
    table->path[sizeof(table->path) - 1] = '\0';
    return true;
}

static bool save_csv_file_at(CSVTable *table, const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }

    for (int r = 0; r < table->row_count; r++) {
        int count = table->cell_counts[r];
        for (int c = 0; c < count; c++) {
            char *raw = cell_value(table, r, c);
            char *escaped = csv_escape(raw);
            fputs(escaped, file);
            free(escaped);
            if (c + 1 < count) {
                fputc(',', file);
            }
        }
        fputc('\n', file);
    }

    fclose(file);
    strncpy(table->path, path, sizeof(table->path) - 1);
    table->path[sizeof(table->path) - 1] = '\0';
    table->dirty = false;
    return true;
}

static bool save_csv_file(CSVTable *table) {
    if (table->path[0] == '\0') {
        return false;
    }
    return save_csv_file_at(table, table->path);
}

static void load_csv_prompt(CSVTable *table) {
    char path[512];
    echo();
    curs_set(1);
    mvprintw(LINES - 2, 0, "Load CSV file: ");
    clrtoeol();
    move(LINES - 2, 16);
    getstr(path);
    noecho();
    curs_set(0);

    if (path[0] == '\0') {
        return;
    }

    table_free(table);
    if (load_csv_file(table, path)) {
        mvprintw(LINES - 1, 0, "Loaded %s", path);
        clrtoeol();
        refresh();
        getch();
    }
}

static void save_as_prompt(CSVTable *table) {
    char path[512];
    echo();
    curs_set(1);
    mvprintw(LINES - 2, 0, "Save as: ");
    clrtoeol();
    move(LINES - 2, 10);
    getstr(path);
    noecho();
    curs_set(0);

    if (path[0] == '\0') {
        return;
    }

    save_csv_file_at(table, path);
    mvprintw(LINES - 1, 0, "Saved to %s", path);
    clrtoeol();
    refresh();
    getch();
}

static void edit_selected_cell(CSVTable *table, int row, int col) {
    if (table == NULL || row < 0 || col < 0) {
        return;
    }

    ensure_row(table, row);

    if (table->cells[row] == NULL) {
        table->cells[row] = calloc(1, sizeof(char *));
        table->cell_counts[row] = 0;
    }

    char buffer[MAX_EDIT_LEN];
    snprintf(buffer, sizeof(buffer), "%s", cell_value(table, row, col));

    echo();
    curs_set(1);
    mvprintw(LINES - 1, 0, "Edit cell [%d,%d]: %s", row + 1, col + 1, buffer);
    clrtoeol();
    move(LINES - 1, 16 + strlen(buffer));
    getstr(buffer);
    noecho();
    curs_set(0);

    set_cell(table, row, col, buffer);
}

static void draw_table(CSVTable *table, int selected_row, int selected_col,
                      int *scroll_row, int *scroll_col) {
    if (table->row_count == 0) {
        append_blank_row(table);
    }

    int rows_on_screen = LINES - 5;
    int cols_on_screen = (COLS - 8) / DEFAULT_WIDTH;
    if (cols_on_screen < 1) {
        cols_on_screen = 1;
    }

    if (selected_row < *scroll_row) {
        *scroll_row = selected_row;
    }
    if (selected_col < *scroll_col) {
        *scroll_col = selected_col;
    }
    if (selected_row >= *scroll_row + rows_on_screen) {
        *scroll_row = selected_row - rows_on_screen + 1;
    }
    if (selected_col >= *scroll_col + cols_on_screen) {
        *scroll_col = selected_col - cols_on_screen + 1;
    }

    erase();

    const char *label = table->path[0] != '\0' ? table->path : "untitled.csv";
    mvprintw(0, 0, "CSV Editor: %s%s", label, table->dirty ? " *" : "");

    int header_y = 1;
    for (int col_index = *scroll_col; col_index < table->max_cols && col_index < *scroll_col + cols_on_screen; col_index++) {
        int x = 8 + (col_index - *scroll_col) * DEFAULT_WIDTH;
        char *label = column_label(col_index);
        mvprintw(header_y, x, "%s", label);
    }

    for (int y = 0; y < rows_on_screen; y++) {
        int row_index = *scroll_row + y;
        if (row_index >= table->row_count) {
            break;
        }

        int x = 0;
        mvprintw(y + 2, x, "%3d |", row_index + 1);
        x += 6;

        for (int col_index = *scroll_col; col_index < table->max_cols && x + DEFAULT_WIDTH < COLS; col_index++) {
            char *value = cell_value(table, row_index, col_index);
            int cell_x = x;
            char buffer[DEFAULT_WIDTH + 1];
            snprintf(buffer, sizeof(buffer), "%.*s", DEFAULT_WIDTH - 1, value);

            if (row_index == selected_row && col_index == selected_col) {
                attron(A_REVERSE);
                mvprintw(y + 2, cell_x, "%*.*s", DEFAULT_WIDTH, DEFAULT_WIDTH, buffer);
                attroff(A_REVERSE);
            } else {
                mvprintw(y + 2, cell_x, "%*.*s", DEFAULT_WIDTH, DEFAULT_WIDTH, buffer);
            }

            x += DEFAULT_WIDTH;
        }
    }

    mvprintw(LINES - 3, 0, "Arrows move, Enter/e edit, s save, S save as, l load, q quit, a add row, d delete row, c add column");
    mvprintw(LINES - 2, 0, "Cell [%d,%d] = %s",
             selected_row + 1, selected_col + 1, cell_value(table, selected_row, selected_col));
    mvprintw(LINES - 1, 0, "Cursor: row %d col %s  Scroll: row %d col %d", selected_row + 1,
             column_label(selected_col), *scroll_row + 1, *scroll_col + 1);
    clrtoeol();
    refresh();
}

static bool prompt_save_before_quit(CSVTable *table) {
    echo();
    curs_set(1);
    mvprintw(LINES - 1, 0, "Save changes before quitting? [y/N]: ");
    clrtoeol();
    char answer[16];
    wgetnstr(stdscr, answer, sizeof(answer) - 1);
    noecho();
    curs_set(0);

    if (strcasecmp(answer, "y") == 0 || strcasecmp(answer, "yes") == 0) {
        return save_csv_file(table);
    }
    return true;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "untitled.csv";
    CSVTable table;
    table_init(&table);

    if (!load_csv_file(&table, path)) {
        fprintf(stderr, "Failed to load CSV file: %s\n", path);
        table_free(&table);
        return 1;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int selected_row = 0;
    int selected_col = 0;
    int scroll_row = 0;
    int scroll_col = 0;
    bool running = true;

    while (running) {
        draw_table(&table, selected_row, selected_col, &scroll_row, &scroll_col);

        int ch = getch();
        switch (ch) {
            case KEY_UP:
                if (selected_row > 0) selected_row--;
                break;
            case KEY_DOWN:
                if (selected_row + 1 < table.row_count) selected_row++;
                break;
            case KEY_LEFT:
                if (selected_col > 0) selected_col--;
                break;
            case KEY_RIGHT:
                selected_col++;
                if (selected_col >= table.max_cols) {
                    selected_col = table.max_cols - 1;
                    if (selected_col < 0) {
                        selected_col = 0;
                    }
                }
                break;
            case KEY_ENTER:
            case 10:
            case 13:
            case 'e':
            case 'E':
                edit_selected_cell(&table, selected_row, selected_col);
                break;
            case 's':
            case 'S':
                if (ch == 'S') {
                    save_as_prompt(&table);
                } else if (!save_csv_file(&table)) {
                    mvprintw(LINES - 1, 0, "Unable to save file.");
                    clrtoeol();
                    refresh();
                    getch();
                }
                break;
            case 'l':
            case 'L':
                load_csv_prompt(&table);
                selected_row = 0;
                selected_col = 0;
                scroll_row = 0;
                scroll_col = 0;
                break;
            case 'a':
            case 'A':
                append_blank_row(&table);
                selected_row = table.row_count - 1;
                selected_col = 0;
                break;
            case 'c':
            case 'C':
                add_column_to_row(&table, selected_row);
                selected_col = table.cell_counts[selected_row] - 1;
                break;
            case 'd':
            case 'D':
                if (table.row_count > 1) {
                    delete_row(&table, selected_row);
                    if (selected_row >= table.row_count) {
                        selected_row = table.row_count - 1;
                    }
                    if (selected_col >= table.max_cols) {
                        selected_col = table.max_cols > 0 ? table.max_cols - 1 : 0;
                    }
                }
                break;
            case 'q':
            case 'Q':
                if (table.dirty) {
                    if (!prompt_save_before_quit(&table)) {
                        running = false;
                    }
                } else {
                    running = false;
                }
                break;
            case 'x':
            case 'X':
                if (table.dirty) {
                    if (!prompt_save_before_quit(&table)) {
                        running = false;
                    }
                } else {
                    running = false;
                }
                break;
            case KEY_RESIZE:
                break;
            default:
                break;
        }
    }

    endwin();
    table_free(&table);
    return 0;
}
