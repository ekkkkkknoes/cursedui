all: cursedui

cursedui: cursedui.c
	gcc -g -o cursedui cursedui.c -lncursesw
