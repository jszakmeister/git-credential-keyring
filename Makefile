all: git-credential-keyring

git-credential-keyring: credential-keyring.c
	gcc -Wall -Wextra -O2 -o git-credential-keyring \
	    `pkg-config --cflags libgnomeui-2.0` \
	    `pkg-config --cflags gnome-keyring-1` \
	    credential-keyring.c \
	    `pkg-config --libs gnome-keyring-1` \
	    `pkg-config --libs libgnomeui-2.0`
