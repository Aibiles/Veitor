/*** include ***/
#pragma region

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
 
#pragma endregion

/*** define ***/
#pragma region

#define VEITOR_VERSION "0.0.1"
#define VEITOR_TAP_STOP 8

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

#pragma endregion

/*** data **/
#pragma region

// 存储一行文本
typedef struct erow
{
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;

// 定义终端配置结构体
struct editorConfig
{
	int cx, cy;			// 文本坐标
	int rx;
	int rowoff;		   // 行偏移量
	int coloff;			// 列偏移量
	int screenrows; // 终端行数
	int screencols;  // 终端列数
	int numrows;	// 打开文本的行数
	erow *row;		 // 存储文本的数组
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios orig_termios;
};

struct editorConfig E;

#pragma endregion

/*** terminal ***/
#pragma region

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

#pragma endregion

/*** row operations ***/
#pragma region

// 将tab转换为指定空格长度
int editorRowCxToRx(erow *row, int cx)
{
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++)
	{
		if (row->chars[j] == '\t')
			rx += (VEITOR_TAP_STOP - 1) - (j % VEITOR_TAP_STOP);
		rx++;
	}

	return rx;
}

void editorUpdateRow(struct erow *row)
{
	int j;
	int tab = 0;
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t')
			tab++;

	free(row->render);
	row->render = malloc(row->size + tab * (VEITOR_TAP_STOP - 1) + 1);

	int index = 0;
	for (j = 0; j < row->size; j++)
	{
		if (row->chars[j] == '\t')
		{
			row->render[index++] = ' ';
			while(index % VEITOR_TAP_STOP != 0) 
				row->render[index++] = ' ';
		}
		else
		{
			row->render[index++] = row->chars[j];
		}
	}
	row->render[index] = '\0';
	row->rsize = index;
}

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

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow((&E.row[at]));

	E.numrows++;
}

#pragma endregion

/*** file i/o ***/
#pragma region

// 打开文件，读取
void editorOpen(char *filename)
{
	free(E.filename);
	E.filename = strdup(filename);

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

#pragma endregion

/*** appenf buffer ***/
#pragma region

// 存储文本内容的结构体
struct abuf
{
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

// 向结构体中添加一行文本
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

#pragma endregion

/*** output ***/
#pragma region

void editorScroll()
{
	E.rx = E.cx;
	if (E.cy < E.numrows)
	{
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}

	// 当文本纵坐标小于行偏移量时
	if (E.cy < E.rowoff)
	{
		E.rowoff = E.cy;
	}
	// 当文本纵坐标大于行偏移量+终端行数时
	if (E.cy >= E.rowoff + E.screenrows)
	{
		E.rowoff = E.cy - E.screenrows + 1;
	}

	if (E.rx < E.coloff)
	{
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff + E.screencols)
	{
		E.coloff = E.rx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < E.screenrows; y++)
	{
		int filerow = y + E.rowoff;
		// 如果行偏移量大于文件内容行数，则不会显示默认文本
		if (filerow >= E.numrows)
		{
			// 如果没有读取文本，在1/3处打印软件名和版本号
			if (E.numrows == 0 && y == E.screenrows / 3)
			{
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome),
										  "Vorpal editor -- version %s", VEITOR_VERSION);
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
			int len = E.row[filerow].rsize - E.coloff;
			// 即列偏移量大于这行文本长度时
			if (len < 0) len = 0;
			if (len > E.screencols)	len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}

		// 清除当前行，清除刷新前的终端内容
		abAppend(ab, "\x1b[K", 3);

		// 为文件文本添加换行符
		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatuBar(struct abuf*ab)
{
	abAppend(ab, "\x1b[7m", 4);

	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines",
							E.filename ? E.filename : "[No Name]", E.numrows);
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d,%d", E.cy + 1, E.cx + 1);
	abAppend(ab, status, len);
	while (len < E.screencols)
	{
		if (E.screencols - len == rlen)
		{
			abAppend(ab, rstatus, rlen);
			break;
		}
		abAppend(ab, " ", 1);
		len++;
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf* ab)
{
	// 清除当前行
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen);
}

// 编辑刷新后的界面
void editorRefreshScreen()
{
	editorScroll();

	// 创建缓冲区
	struct abuf ab = ABUF_INIT;

	// 隐藏光标，移动到左上角
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);	// 绘制波浪线或文本
	editorDrawStatuBar(&ab);
	editorDrawMessageBar(&ab);

	// 移动光标，显示光标
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, 
															(E.rx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

#pragma endregion

/*** input ***/
#pragma region

// 控制光标移动
void editorMoveCursor(int key)
{
	// 判断下一行是否存在并获取本行
	erow * row = (E.cy > E.numrows) ? NULL : &E.row[E.cy];

	switch (key)
	{
	case ARROW_LEFT:
		if (E.cx != 0)
		{
			E.cx--;
		}
		else if (E.cy != 0)
		{
			row = &E.row[E.cy - 1];
			E.cy--;
			E.cx = row->size;
		}
		break;
	case ARROW_RIGHT:
		if (row && E.cx < row->size)
		{
			E.cx++;
		}
		else if (row && E.cx == row->size)
		{
			E.cy++;
			E.cx = 0;
		}
		break;
	case ARROW_UP:
		if (E.cy != 0)
			E.cy--;
		break;
	case ARROW_DOWN:
		// 当文本坐标小于文本行数时
		if (E.cy < E.numrows )
			E.cy++;
		break;
	}

	row = (E.cy > E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen)
		E.cx = rowlen;

}

// 按键映射
void editorProcessKeypress()
{
	int c = editorReadKey();

	switch (c)
	{
	case CTRL_KEY('q'):
		// 退出时清屏
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;

	case HOME_KEY:
		E.cx = 0;
		break;
	case END_KEY:
		if (E.cy < E.numrows)
			E.cx = E.row[E.cy].size;
		break;

	case PAGE_UP:
	case PAGE_DOWN:
	{
		// 页面开头的上一页，结尾的下一页
		if (c == PAGE_UP)
		{
			E.cy = E.rowoff;
		}
		else if (c == PAGE_DOWN)
		{
			E.cy = E.rowoff + E.screenrows - 1;
			if (E.cy > E.numrows) E.cy = E.numrows;
		}

		int times = E.screenrows-1;
		while (times--)
			editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
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

#pragma endregion

/*** init ***/
#pragma region

// 初始化窗口，文本信息
void initEditor()
{
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	if (getWindowSize(&(E.screenrows), &(E.screencols)) == -1)
	{
		die("getWindowSize");
	}
	
	E.screenrows -= 2;
}

int main(int argc, char *args[])
{
	enableRawMode();
	initEditor();
	// if (argc >= 2)
		// editorOpen(args[1]);
	editorOpen("../../src/Makefile");

	editorSetStatusMessage("HELP: Ctrl-Q = quit");

	while (1)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}

#pragma endregion