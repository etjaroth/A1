#!/bin/bash
clear
if [ ${#} -eq 0 ]
then
    echo 'Incorrect number of arguments' >&2
fi

otherArgs=$()
if [ ${#} -ge 2 ]
then
    for i in ${@:2}
    do
        otherArgs="${otherArgs} -l${i}"
    done
fi

echo "" | cat > Makefile <<- EOM
CXX = gcc
LIBS = -pthread \`pkg-config --libs gstreamer-1.0\`
CLIBS = -pthread \`pkg-config --cflags gstreamer-1.0\`
CFLAGS = -MMD -g -Werror \${CLIBS}
EXEC = ${1}
EOM

echo "OBJECTS = " | tr -d '\n' | cat >> Makefile

ls -1 *.c | sed -e 's/\..*$//' | sed 's/$/.o /' | tr -d '\n' | cat >> Makefile

TAB="$(printf '\t')"
cat >> Makefile <<- EOM

DEPENDS = \${OBJECTS:.o=.d}

\${EXEC}: \${OBJECTS}
${TAB}\${CXX} \${OBJECTS} -o \${EXEC} \${LIBS} ${otherArgs}

-include \${DEPENDS}

.PHONY: clean sweep noMoreExec

clean: 
${TAB}rm \${OBJECTS} \${EXEC} \${DEPENDS}

sweep:
${TAB}rm \${OBJECTS} \${DEPENDS}

noMoreExec:
${TAB}rm \${EXEC}
EOM

# mv ./Makefile ../Makefile
# cd ..
make noMoreExec
make
if [ ${?} -eq 0 ]
then
    make sweep
    clear
    echo "Success!"
fi
# rm Makefile
