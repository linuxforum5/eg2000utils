NAME=cas2tap
gcc src/$NAME.c -o bin/$NAME
cp bin/$NAME ~/.local/bin/$NAME

NAME=cmd2tap
gcc src/$NAME.c -o bin/$NAME
cp bin/$NAME ~/.local/bin/$NAME

NAME=tap2wav
gcc src/$NAME.c -o bin/$NAME
cp bin/$NAME ~/.local/bin/$NAME
