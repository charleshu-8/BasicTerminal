FILE = my_shell.c
EXEC = my_shell

main: $(FILE)
	@gcc $(FILE) -o $(EXEC) -I -Wall -Wextra

clean:
	@rm -f $(EXEC)
