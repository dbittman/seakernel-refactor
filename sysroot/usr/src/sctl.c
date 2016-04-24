int main(int argc, char **argv)
{
	if(argc == 1)
		return 0;

	switch(argv[1][0]) {
		case '0':
			syscall(512);
	}
	return 0;
}

