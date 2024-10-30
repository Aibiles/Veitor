/*** include ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** define ***/

#define VORPAL_VERSION "0.0.1"

// (a & 0x1f) =  (11000001 & 00011111) = 1
#define CTRL_KEY(k) ((k) & 0x1f)

// 配置按键绑定枚举变量
enum editorKey
{
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
	DEL_KEY
};

/*** data **/

// 存储一行文本
typedef struct erow
{
	int size;
	char *chars;
} erow;

// 定义终端配置结构体
struct editorConfig
{
	int cx, cy;		// 光标坐标
	int rowoff;		// 行偏移量
	int screenrows; // 终端行数
	int screencols; // 终端列数
	int numrows;	// 打开文本的行数
	erow *row;		// 存储文本的数组
	struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

// 当出现错误时清空屏幕内容并将光标一到左上角，打印错误信息
void die(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(2);
}

void disableRawMode()
{
	// 例如出现echo "test" | ./vorpal，并非终端输入而是管道传输
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &(E.orig_termios)) == -1)
	{
		die("tcsetattr");
	}
}

// 用于获得和初始化终端属性
void enableRawMode()
{
	// 获得当前终端属性
	if (tcgetattr(STDIN_FILENO, &(E.orig_termios)) == -1)
	{
		die("tcgetattr");
	}
	atexit(disableRawMode);

	// 配置终端属性
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	// 加载终端属性
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
	{
		die("tcsetattr");
	}
}

int editorReadKey()
{
	int nread;
	char c;
	// 循环直到从终端读取到指令
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
	{
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}
	if (c == '\x1b')
	{
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';
		if (seq[0] == '[')
		{
			if (seq[1] >= '0' && seq[1] <= '9')
			{
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if (seq[2] == '~')
				{
					switch (seq[1])
					{
					case '1':
						return HOME_KEY;
					case '3':
						return DEL_KEY;
					case '4':
						return END_KEY;
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					case '7':
						return HOME_KEY;
					case '8':
						return END_KEY;
					}
				}
			}
			else
			{
				switch (seq[1])
				{
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;

				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
				}
			}
		}
		else if (seq[0] == 'O')
		{
			switch (seq[1])
			{
			case 'H':
				return HOME_KEY;
			case 'F':
				return END_KEY;
			}
		}
		return '\x1b';
	}
	else
		return c;
}

int getCursorPosition(int *rows, int *cols)
{
	char buf[32];
	unsigned int i = 0;

	// 查询光标位置
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	while (i < sizeof(buf) - 1)
	{
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}

	buf[i] = '\0';
	// 不需要的转义字符和控制
	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols)
{
	struct winsize ws;
	// 是否得到窗口大小
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
	{
		// 移动到右下角
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;
		return getCursorPosition(rows, cols);
	}
	else
	{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** row operations ***/

// 输出缓冲区，立即展示一页内的所有内容
void editorAppendRow(char *s, size_t len)
{
	// realloc可以调整分配内存大小，如果小了会截断
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	E.numrows++;
}

/*** file i/o ***/

// 打开文件，读取
void editorOpen(char *filename)
{

	FILE *fp = fopen(filename, "r");
	if (!fp)
		die("fopen");

	char *line = NULL;
	ssize_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1)
	{
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
							   line[linelen - 1] == '\r'))
			linelen--;
		editorAppendRow(line, linelen);
	}

	free(line);
	fclose(fp);
}

/*** appenf buffer ***/

struct abuf
{
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL)
		return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab)
{
	free(ab->b);
}

/*** output ***/

void editorDrawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < E.screenrows; y++)
	{
		int filerow = E.rowoff + y;
		// 如果行偏移量大于文件内容行数，则不会显示
		if (filerow >= E.numrows)
		{
			// 如果没有读取文本，在1/3处打印软件名和版本号
			if (E.numrows == 0 && y == E.screenrows / 3)
			{
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome),
										  "Vorpal editor -- version %s", VORPAL_VERSION);
				if (welcomelen > E.screencols)
					welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen) / 2;
				if (padding--)
					abAppend(ab, "~", 1);
				while (padding--)
					abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			}
			else
			{
				abAppend(ab, "~", 1);
			}
		}
		else
		{
			int len = E.row[filerow].size;
			if (len > E.screencols)
				len = E.screencols;
			abAppend(ab, E.row[filerow].chars, len);
		}

		// 清除当前行，清除打开文本前的终端内容
		abAppend(ab, "\x1b[K", 3);

		// 为文件文本添加换行符
		if (y < E.screenrows - 1)
		{
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreashScreen()
{
	// 创建缓冲区
	struct abuf ab = ABUF_INIT;

	// 隐藏光标，移动到左上角
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	// 绘制波浪线
	editorDrawRows(&ab);

	// 移动光标，显示光标
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key)
{
	switch (key)
	{
	case ARROW_LEFT:
		if (E.cx != 0)
			E.cx--;
		break;
	case ARROW_RIGHT:
		if (E.cx != E.screencols - 1)
			E.cx++;
		break;
	case ARROW_UP:
		if (E.cy != 0)
			E.cy--;
		break;
	case ARROW_DOWN:
		if (E.cy != E.screenrows - 1)
			E.cy++;
		break;
	}
}

void editorProcessKeypress()
{
	int c = editorReadKey();

	switch (c)
	{
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;

	case HOME_KEY:
		E.cx = 0;
		break;
	case END_KEY:
		E.cx = E.screencols - 1;
		break;

	case PAGE_UP:
	case PAGE_DOWN:
	{
		int times = E.screenrows;
		while (times--)
			if (c == PAGE_UP)
				editorMoveCursor(ARROW_UP);
			else
				editorMoveCursor(ARROW_DOWN);
	}
	break;

	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		editorMoveCursor(c);
		break;
	}
}

/*** init ***/

void initEditor()
{
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.numrows = 0;
	E.row = NULL;

	if (getWindowSize(&(E.screenrows), &(E.screencols)) == -1)
	{
		die("getWindowSize");
	}
}

int main(int argc, char *args[])
{
	enableRawMode();
	initEditor();
	if (argc >= 2)
		editorOpen(args[1]);

	while (1)
	{
		editorRefreashScreen();
		editorProcessKeypress();
	}

	return 0;
}
