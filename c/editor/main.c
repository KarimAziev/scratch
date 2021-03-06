// https://viewsourcecode.org/snaptoken/kilo/02.enteringRawMode.html

/** includes **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/** defines **/

#define MY_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editor_key
  {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
  };

/** declarations **/

void die (const char *s);
int editor_read_key ();
int get_cursor_position (int *rows, int *cols);

/** data **/

typedef struct erow
{
  int size;
  char *chars;
} erow;

struct world_atom
{
  int cx, cy;
  int rowoff;
  int rows;
  int cols;
  int numrows;
  erow *row;
  struct termios orig_termios;
};

struct world_atom world;

/** util **/

// COL / ROW

// https://stackoverflow.com/questions/8257714/how-to-convert-an-int-to-string-in-c#8257728

typedef struct abuf
{
  char *b;
  int len;
} abuf;

#define ABUF_INIT {NULL, 0}

void ab_append (struct abuf *ab, const char *s, int len)
{
  char *new = realloc (ab->b, ab->len + len);

  if (new == NULL) return;

  memcpy (&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void ab_write (struct abuf *ab)
{
  write (STDOUT_FILENO, ab->b, ab->len);
}

void ab_free (struct abuf *ab)
{
  free (ab->b);
}

int get_byte_size_of_int_as_char (int n)
{
  return (int) ((ceil (log10 (n)) + 1) * sizeof (char));
}

// clear everything at end of row
void clear_row (struct abuf *ab)
{
  ab_append (ab, "\x1b[K", 3);
}

void cursor_hide (struct abuf *ab)
{
  ab_append (ab, "\x1b[?25l", 6);
}

void cursor_show (struct abuf *ab)
{
  ab_append (ab, "\x1b[?25h", 6);
}

// TODO: Ensure x or y does not go out of bounds of window.
void cursor_goto (struct abuf *ab, int x, int y)
{
  char buf[32];

  snprintf (buf, sizeof (buf), "\x1b[%d;%dH", y + 1, x + 1);
  ab_append (ab, buf, strlen (buf));
}

// TODO: What didn't it like about this?
void xcursor_goto (struct abuf *ab, int x, int y)
{
  int xlen = get_byte_size_of_int_as_char (x);
  int ylen = get_byte_size_of_int_as_char (y);
  int olen = 4; // \x1b [ H, and the ;
  int len = xlen + ylen + olen;
  char str[len]; // Slot for \x1b[_;_H

  sprintf (str, "\x1b[%d;%dH", x, y);
  // write (STDOUT_FILENO, str, len); // Position cursor 12;40H would center on an 80x24 size
  ab_append (ab, str, len);
}

void clear_screen (struct abuf *ab)
{
  ab_append (ab, "\x1b[2J", 4); // Clear screen
}

void clear_and_reposition (struct abuf *ab)
{
  clear_screen (ab);
  cursor_goto (ab, 1, 1);
}

/** terminal **/

void die (const char *s)
{
  struct abuf ab = ABUF_INIT;

  clear_and_reposition (&ab);
  ab_free (&ab);
  perror (s);
  exit (1);
}

void disable_raw_mode ()
{
  if (-1 == tcsetattr (STDIN_FILENO, TCSAFLUSH, &world.orig_termios))
    die ("tcsetattr");
}

void enable_raw_mode ()
{
  if (-1 == tcgetattr (STDIN_FILENO, &world.orig_termios)) die("tcgetattr");
  atexit (disable_raw_mode);

  struct termios raw = world.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1; // Every 10th of second redraw / skip the read (stop block).

  if (-1 == tcsetattr (STDIN_FILENO, TCSAFLUSH, &raw)) die("tcsetattr");
}

int get_window_size (int *rows, int *cols)
{
  struct winsize ws;

  if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
      // Fallback for non-ioctl supporting systems
      if (write (STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;

      return get_cursor_position (rows, cols);
    }
  else
    {
      *cols = ws.ws_col;
      *rows = ws.ws_row;

      return 0;
    }
}

int editor_read_key ()
{
  int nread;
  char c;

  while ((nread = read (STDIN_FILENO, &c, 1)) != 1)
    {
      if (nread == -1 && errno != EAGAIN) die ("read");
    }

  // A control code like C- was hit.
  if (c == '\x1b')
    {
      char seq[3];

      if (read (STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
      if (read (STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

      if (seq[0] == '[')
        {
          if (seq[1] >= 0 && seq[1] <= '9')
            {
              if (read (STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
              if (seq[2] == '~')
                {
                  switch (seq[1])
                    {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return  PAGE_UP;
                    case '6': return  PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
                }
            }
          else
            {
              // Parse the arrow keys
              switch (seq[1])
                {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        }
      else if (seq[0] == 'O')
        {
          switch (seq[1])
            {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

      return '\x1b';
    }

  return c;
}

int get_cursor_position (int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  if (write (STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof (buf) - 1)
    {
      if (read (STDIN_FILENO, &buf[i], 1) != 1) break;
      if (buf[i] == 'R') break;
      i++;
    }
  buf[i] = '\0';

  // printf ("\r\n&buf[1]: '%s'\r\n", &buf[1]);
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf (&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

// Calculation to center the screen
int get_padding (int cols, int len) { return (cols - len) / 2; }

// Write the anchor, then pad out to the middle.
void do_padding (struct abuf *ab, int pad)
{
  if (pad)
    {
      ab_append (ab, "~", 1);
      pad--;
    }
  while (pad--) ab_append (ab, " ", 1);
}

/** row operations **/

void editor_append_row (char *s, size_t len)
{
  world.row = realloc (world.row, sizeof (erow) * (world.numrows + 1));

  int at = world.numrows;
  world.row[at].size = len;
  world.row[at].chars = malloc (len + 1);
  memcpy (world.row[at].chars, s, len);
  world.row[at].chars[len] = '\0';
  world.numrows++;
}

/** file i/o **/
void editor_open (char *filename)
{
  FILE *fp = fopen (filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline (&line, &linecap, fp)) != -1)
    {
      if (linelen != -1)
        {
          while (linelen > 0 && (line[linelen -1] == '\n' ||
                                 line[linelen -1] == '\r'))
            linelen--;

          editor_append_row (line, linelen);
        }
    }
  free (line);
  fclose (fp);
}

/** output **/

void editor_scroll ()
{
  if (world.cy < world.rowoff)
    {
      world.rowoff = world.cy;
    }

  if (world.cy >= world.rowoff + world.rows)
    {
      world.rowoff = world.cy - world.rows + 1;
    }
}

int get_out_col_max (int n) { return n > world.cols ? world.cols : n; }

void out_row_contents (struct abuf *ab, int y)
{
  int len = world.row[y].size;
  int n = get_out_col_max (len);
  ab_append (ab, world.row[y].chars, n);
}

void out_welcome (struct abuf *ab)
{
  char w[80];
  int wlen = snprintf (w, sizeof (w), "xxx -- version %s", MY_VERSION);
  wlen = get_out_col_max (wlen);
  int padding = get_padding (world.cols, wlen);
  do_padding (ab, padding);
  ab_append (ab, w, wlen);
}

void out_welcome_or_append (abuf *ab, int y, int numrows, int rows)
{
  if (numrows == 0 && y == rows / 3)
    {
      out_welcome (ab);
    }
  else
    {
      // Write empty line marker
      ab_append (ab, "~", 1);
    }
}

void out_row_or_beyond_buffer (abuf *ab, int y, int numrows, int rows, int rowoff)
{
  int filerow = y + rowoff;

  if (filerow >= numrows)
    {
      out_welcome_or_append (ab, y, numrows, rows);
    }
  else
    {
      out_row_contents (ab, filerow);
    }
}

void out_maybe_eol (abuf *ab, int y, int rows)
{
  if (y < rows - 1)
    {
      ab_append (ab, "\r\n", 2);
    }
}

void editor_draw_rows (struct abuf *ab)
{
  int y;

  for (y = 0; y < world.rows; y++)
    {
      out_row_or_beyond_buffer (ab, y, world.numrows, world.rows, world.rowoff);
      clear_row (ab);
      out_maybe_eol (ab, y, world.rows);
    }
}

void editor_refresh_screen ()
{
  editor_scroll ();

  struct abuf ab = ABUF_INIT;

  // Ultimately, the rows we draw etc. we would receive
  // from a remote data source, and run the refresh on receipt of it.
  cursor_hide (&ab);
  editor_draw_rows (&ab);
  cursor_goto (&ab, world.cx, world.cy);
  cursor_show (&ab);

  ab_write (&ab);
  ab_free (&ab);
}

/** input **/

void editor_move_cursor (int key)
{
  switch (key)
    {
    case ARROW_LEFT:
      if (world.cx > 0)
        {
          world.cx--;
        }
      break;

    case ARROW_RIGHT:
      if (world.cx != world.cols - 1)
        {
          world.cx++;
        }
      break;

    case ARROW_UP:
      if (world.cy > 0)
        {
          world.cy--;
        }
      break;

    case ARROW_DOWN:
      if (world.cy < world.numrows)
        {
          world.cy++;
        }
      break;
    }
}

void editor_process_keypress ()
{
  int c = editor_read_key ();

  // TODO: Here, we would want to send it out / process it.
  switch (c)
    {
    case CTRL_KEY('q'):
      {
        struct abuf ab = ABUF_INIT;
        clear_and_reposition (&ab);
        ab_write (&ab);
        ab_free (&ab);
        exit (0);
      }
      break;

    case HOME_KEY:
      world.cx = 0;
      break;

    case END_KEY:
      world.cx = world.cols - 1;
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = world.rows;
        while (times--)
          {
            editor_move_cursor (c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
          }
        break;
      }

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editor_move_cursor (c);
      break;
    }
}


/** init **/

void init_world ()
{
  world.cx = 10;
  world.cy = 10;
  world.rowoff = 0;
  world.numrows = 0;
  world.row = NULL;

  if (get_window_size (&world.rows, &world.cols) == -1) die("get_window_size");
}

int main (int argc, char *argv[])
{
  enable_raw_mode ();
  init_world ();

  if (argc >= 2)
    {
      editor_open (argv[1]);
    }

  while (1)
    {
      editor_refresh_screen ();
      editor_process_keypress ();
    }

  return 0;
}
