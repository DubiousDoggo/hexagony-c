
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRINGIFY(x) #x
#define STRINGIZE(x) STRINGIFY(x)

#define MEM_FMT_LEN 2
#define MEM_FMT "%" STRINGIZE(MEM_FMT_LEN) "d"

enum direction { NW, NE, E, SE, SW, W };
enum axis { X, Y, Z };
enum neighbor { LEFT = -1, RIGHT = 1 };

struct program_cell {
    char value;
    bool debug;
};

// Memory is defined as an infinite hexagonal grid where each egde is a value.
// In this implementation, memory is indexed with the axial coordinates of an offset hexagonal grid, corresponding to a
// cubic offset of z+1. Each hexagon in this offset grid stores 3 values, one for each cubic axis, corresponding to the
// edges of the original grid. Since the memory is theoretically infinite, allocation grows outward in rings as needed.
// Storing each ring sequentially reduces memory overhead at the cost of a bit of math.
struct memory_cell {
    int value[3];
};

struct memory_pointer {
    long p, q;
    enum axis axis;
    enum { IN, OUT } direction;
};

// x,y axial offests for each hexagonal direction
const struct {
    long dp, dq;
} direction_offset[] = {
    [NW] = {0, -1}, [NE] = {-1, 0}, [E] = {-1, 1}, [SE] = {0, 1}, [SW] = {1, 0}, [W] = {1, -1},
};

const char *direction_name[6] = {
    [NW] = "NORTH WEST", [NE] = "NORTH EAST", [E] = "EAST", [SE] = "SOUTH EAST", [SW] = "SOUTH WEST", [W] = "WEST",
};
const char *axis_name[3] = {[X] = "X", [Y] = "Y", [Z] = "Z"};

// mathematical modulus
long modulo(long a, long b) {
    const long result = a % labs(b);
    return (result >= 0 ? result : result + b) * (b >= 0 ? 1 : -1);
}

// Convert x,y axial coordinates to index for sequentially stored rows along the z axis
ssize_t axial_to_index(long p, long q, long rings) {
    long x = p;
    long y = q;
    long z = -p - q;
    if (labs(x) + labs(y) + labs(z) > 2 * (rings - 1))
        return -1;
    ssize_t i = (3 * rings * (rings - 1)) / 2;
    i += y + -z * (rings * 2 - 1);
    i += z * (labs(z) + 1) / 2;
    return i;
}

// Convert x,y axial coordinated to a radial index, see memory_cell
size_t axial_to_mem_index(long p, long q) {
    long x = p;
    long y = q;
    long z = -p - q;
    // The ring number is the hexagonal distance from the origin.
    // This is the same as half the manhattan distance in cubic coordinates.
    size_t ring = (labs(x) + labs(y) + labs(z)) / 2;
    size_t i = ring > 0 ? (3 * ring * (ring - 1) + 1) : 0;
    // find the clockwise offset from the closest corner of the ring
    if (x <= 0 && y < 0)
        i += ring * 0 + labs(x);
    if (y >= 0 && z > 0)
        i += ring * 1 + labs(y);
    if (z <= 0 && x < 0)
        i += ring * 2 + labs(z);
    if (x >= 0 && y > 0)
        i += ring * 3 + labs(x);
    if (y <= 0 && z < 0)
        i += ring * 4 + labs(y);
    if (z >= 0 && x > 0)
        i += ring * 5 + labs(z);
    return i;
}

struct memory_cell *realloc_memory(struct memory_cell *memory, long old_rings, long new_rings) {
    size_t old_size = (3 * old_rings * (old_rings - 1) + 1);
    size_t new_size = (3 * new_rings * (new_rings - 1) + 1);
    memory = realloc(memory, new_size * sizeof(struct memory_cell));
    for (size_t i = old_size; i < new_size; i++) {
        memory[i].value[X] = 0;
        memory[i].value[Y] = 0;
        memory[i].value[Z] = 0;
    }
    return memory;
}

// gets the memory cell at axial p,q and reallocates memory if the index is out of range
struct memory_cell *get_memory_cell(long p, long q, struct memory_cell **memory, long *rings) {
    size_t index = axial_to_mem_index(p, q);
    while (index >= (3 * *rings * (*rings - 1) + 1)) {
        *memory = realloc_memory(*memory, *rings, *rings + 1);
        ++*rings;
    }
    return *memory + index;
}

int *get_memory_edge(struct memory_pointer ptr, struct memory_cell **memory, long *rings) {
    return &get_memory_cell(ptr.p, ptr.q, memory, rings)->value[ptr.axis];
}

int *get_neighbor(struct memory_pointer ptr, enum neighbor neighbor, struct memory_cell **memory, long *rings) {
    struct memory_cell *cell;
    long xyz[3] = {ptr.p, ptr.q, -ptr.p - ptr.q};
    if (ptr.direction == OUT) {
        // TODO I'm pretty sure this can be simplified with a bit of mathing
        switch (ptr.axis) {
        case X:
            xyz[X] += 1;
            if (neighbor == LEFT)
                xyz[Z] -= 1;
            if (neighbor == RIGHT)
                xyz[Y] -= 1;
            break;
        case Y:
            xyz[Y] += 1;
            if (neighbor == LEFT)
                xyz[X] -= 1;
            if (neighbor == RIGHT)
                xyz[Z] -= 1;
            break;
        case Z:
            xyz[Z] += 1;
            if (neighbor == LEFT)
                xyz[Y] -= 1;
            if (neighbor == RIGHT)
                xyz[X] -= 1;
        }
    }
    cell = get_memory_cell(xyz[X], xyz[Y], memory, rings);
    return &cell->value[modulo(ptr.axis + neighbor, 3)];
}

void move_mp(struct memory_pointer *ptr, enum neighbor neighbor) {
    long xyz[3] = {ptr->p, ptr->q, -ptr->p - ptr->q};
    if (ptr->direction == OUT) {
        switch (ptr->axis) {
        case X:
            xyz[X] += 1;
            if (neighbor == LEFT)
                xyz[Z] -= 1;
            if (neighbor == RIGHT)
                xyz[Y] -= 1;
            break;
        case Y:
            xyz[Y] += 1;
            if (neighbor == LEFT)
                xyz[X] -= 1;
            if (neighbor == RIGHT)
                xyz[Z] -= 1;
            break;
        case Z:
            xyz[Z] += 1;
            if (neighbor == LEFT)
                xyz[Y] -= 1;
            if (neighbor == RIGHT)
                xyz[X] -= 1;
        }

        ptr->direction = IN;
    } else {
        ptr->direction = OUT;
    }
    ptr->axis = modulo(ptr->axis + neighbor, 3);
    ptr->p = xyz[X];
    ptr->q = xyz[Y];
}

void print_memory(struct memory_cell *memory, long rings) {

    for (long z = rings - 1; z >= 1 - rings; z--) {
        long x = rings - 1;
        long y = 1 - rings;
        if (z > 0)
            x -= z;
        if (z < 0)
            y -= z;

        for (long s = 0; s < abs(z); s++)
            printf("  %*s ", MEM_FMT_LEN, "");
        for (long p = x, q = y; abs(p) + abs(q) + abs(z) <= rings; --p, q++) {
            struct memory_cell *cell = get_memory_cell(p, q, &memory, &rings);
            printf("    " MEM_FMT " %*s ", cell->value[Z], MEM_FMT_LEN, "");
        }
        putchar('\n');

        for (long s = 0; s < abs(z); s++)
            printf("  %*s ", MEM_FMT_LEN, "");
        for (long p = x, q = y; abs(p) + abs(q) + abs(z) <= rings; --p, q++) {
            struct memory_cell *cell = get_memory_cell(p, q, &memory, &rings);
            printf(". " MEM_FMT " ' " MEM_FMT " ", cell->value[X], cell->value[Y]);
        }
        puts(".");
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("No filename specified.\n", stderr);
        return EXIT_FAILURE;
    }
    long program_rings = 1;
    size_t program_size = (3 * program_rings * (program_rings - 1) + 1); // ring'th centered hexagonal number
    struct program_cell *program = malloc(program_size * sizeof(struct program_cell));
    {
        FILE *source = fopen(argv[1], "r");
        if (source == NULL) {
            perror("Error opening file");
            return EXIT_FAILURE;
        }
        bool debug_next = false;
        int c;
        size_t i = 0;
        while ((c = fgetc(source)) != EOF) {
            if (c == '`')
                debug_next = true;
            else if (!isspace(c)) {
                if (i >= program_size) {
                    ++program_rings;
                    program_size = (3 * program_rings * (program_rings - 1) + 1);
                    program = realloc(program, program_size * sizeof(struct program_cell));
                }
                program[i].value = c;
                program[i].debug = debug_next;
                debug_next = false;
                i++;
            }
        }
        while (i < program_size) {
            program[i].value = '.';
            program[i].debug = false;
            i++;
        }
        fclose(source);
    }

    struct IP {
        long p, q;
        enum direction direction;
        bool ignore_next;
    } IPs[6] = {
        {0, -(program_rings - 1), E, false},                     // NW
        {-(program_rings - 1), 0, SE, false},                    // NE
        {-(program_rings - 1), +(program_rings - 1), SW, false}, // E
        {0, +(program_rings - 1), W, false},                     // SE
        {+(program_rings - 1), 0, NW, false},                    // SW
        {+(program_rings - 1), -(program_rings - 1), NE, false}, // W
    };
    int IP_index = 0;

    long memory_rings = 1;
    struct memory_cell *memory = calloc(1, sizeof(struct memory_cell));
    struct memory_pointer MP = {0, 0, Z, IN};

    bool force_debug = false;
    struct program_cell *instruction;
    do {
        struct IP *IP = IPs + IP_index;
        if (IP->ignore_next) {
            IP->ignore_next = false;
        } else {
            instruction = program + axial_to_index(IP->p, IP->q, program_rings);
            if (instruction->debug || force_debug) {
                printf("\nPaused on '%c'\n", instruction->value);
                printf("Active IP: %d\n", IP_index);
                int digits = log10(program_rings);
                for (int i = 0; i < 6; i++)
                    printf("IP %d (%+*ld, %+*ld) %s\n", i, digits, IPs[i].p, digits, IPs[i].q,
                           direction_name[IPs[i].direction]);
                print_memory(memory, memory_rings);
                printf("MP: (%+ld, %+ld) %s %s\n", MP.p, MP.q, axis_name[MP.axis],
                       MP.direction == IN ? "INWARDS" : "OUTWARDS");
                printf("Press enter to continue: ");
                getchar();
            }
            if (isalpha(instruction->value)) // set MP to value
                *get_memory_edge(MP, &memory, &memory_rings) = instruction->value;
            else if (isdigit(instruction->value)) {
                // multiply the current memory edge by 10 and add the corresponding digit. If the current edge has a
                // negative value, the digit is subtracted instead of added.
                int *edge = get_memory_edge(MP, &memory, &memory_rings);
                *edge *= 10;
                *edge += (*edge < 0 ? -1 : 1) * instruction->value - '0';
            } else {
                switch (instruction->value) {
                // no-op.
                case '.': break;

                // terminates the program.
                case '@': break;

                // increments the current memory edge.
                case ')': ++*get_memory_edge(MP, &memory, &memory_rings); break;

                // decrements the current memory edge.
                case '(': --*get_memory_edge(MP, &memory, &memory_rings); break;

                // sets the current memory edge to the sum of the left and right neighbours.
                case '+':
                    *get_memory_edge(MP, &memory, &memory_rings) = *get_neighbor(MP, LEFT, &memory, &memory_rings) +
                                                                   *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                // sets the current memory edge to the difference of the left and right neighbours (left - right).
                case '-':
                    *get_memory_edge(MP, &memory, &memory_rings) = *get_neighbor(MP, LEFT, &memory, &memory_rings) -
                                                                   *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                // sets the current memory edge to the product of the left and right neighbours.
                case '*':
                    *get_memory_edge(MP, &memory, &memory_rings) = *get_neighbor(MP, LEFT, &memory, &memory_rings) *
                                                                   *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                // sets the current memory edge to the quotient of the left and right neighbours (left / right).
                case ':':
                    *get_memory_edge(MP, &memory, &memory_rings) = *get_neighbor(MP, LEFT, &memory, &memory_rings) /
                                                                   *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                // sets the current memory edge to the modulo of the left and right neighbours (left % right)
                case '%':
                    *get_memory_edge(MP, &memory, &memory_rings) = *get_neighbor(MP, LEFT, &memory, &memory_rings) %
                                                                   *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                // multiplies the current memory edge by -1.
                case '~': *get_memory_edge(MP, &memory, &memory_rings) *= -1; break;

                // reads a single byte from STDIN and sets the current memory edge to its value, or -1 if EOF
                // reached.
                case ',': *get_memory_edge(MP, &memory, &memory_rings) = getchar(); break;

                // reads and discards from STDIN until a digit, a - or a + is found. Then reads as many characters
                // as possible to form a valid (signed) decimal integer and sets the current memory edge to its
                // value. Returns 0 once EOF is reached.
                case '?': {
                    int ch = 0;
                    do {
                        ch = getchar();
                    } while (ch != EOF && !isdigit(ch) && ch != '+' && ch != '-');
                    int *edge = get_memory_edge(MP, &memory, &memory_rings);
                    *edge = 0;
                    if (ch != EOF) {
                        ungetc(ch, stdin);
                        scanf("%d", edge);
                    }
                } break;

                // takes the current memory edge modulo 256 (positive) and writes the corresponding byte to STDOUT.
                case ';': printf("%c", (char)modulo(*get_memory_edge(MP, &memory, &memory_rings), 256)); break;

                // writes the decimal representation of the current memory edge to STDOUT.
                case '!': printf("%d", *get_memory_edge(MP, &memory, &memory_rings)); break;

                // is a jump. When executed, the IP completely ignores the next command in its current direction.
                case '$': IP->ignore_next = true; break;

                // /, \, _, | are mirrors. They reflect the IP in the direction you'd expect. For completeness, the
                // following table shows how they deflect an incoming IP. The top row corresponds to the current
                // direction of the IP, the left column to the mirror, and the table cell shows the outgoing
                // direction of the IP:
                //        cmd │ NW NE  E SE SW  W
                //      ──────┼────────────────────
                //         /  │  E NE NW  W SW SE
                //         \  │ NW  W SW SE  E NE
                //         _  │ SW SE  E NE NW  W
                //         |  │ NE NW  W SW SE  E
                case '/':
                    switch (IP->direction) {
                    case NW: IP->direction = E; break;
                    case NE: IP->direction = NE; break;
                    case E: IP->direction = NW; break;
                    case SE: IP->direction = W; break;
                    case SW: IP->direction = SW; break;
                    case W: IP->direction = SE; break;
                    }
                    break;
                case '\\':
                    switch (IP->direction) {
                    case NW: IP->direction = NW; break;
                    case NE: IP->direction = W; break;
                    case E: IP->direction = SW; break;
                    case SE: IP->direction = SE; break;
                    case SW: IP->direction = E; break;
                    case W: IP->direction = NE; break;
                    }
                    break;
                case '_':
                    switch (IP->direction) {
                    case NW: IP->direction = SW; break;
                    case NE: IP->direction = SE; break;
                    case E: IP->direction = E; break;
                    case SE: IP->direction = NE; break;
                    case SW: IP->direction = NW; break;
                    case W: IP->direction = W; break;
                    }
                    break;
                case '|':
                    switch (IP->direction) {
                    case NW: IP->direction = NE; break;
                    case NE: IP->direction = NW; break;
                    case E: IP->direction = W; break;
                    case SE: IP->direction = SW; break;
                    case SW: IP->direction = SE; break;
                    case W: IP->direction = E; break;
                    }
                    break;

                // < and > act as either mirrors or branches, depending on the incoming direction. The cells
                // indicated as ?? are where they act as branches. In these cases, if the current memory edge is
                // positive, the IP takes a 60° right turn (e.g. < turns E into SE). If the current memory edge is
                // zero or negative, the IP takes a 60° left turn (e.g. < turns E into NE).
                //        cmd │ NW NE  E SE SW  W
                //      ──────┼────────────────────
                //         <  │  W SW ?? NW  W  E
                //         >  │ SE  E  W  E NE ??
                case '<':
                    switch (IP->direction) {
                    case NW: IP->direction = W; break;
                    case NE: IP->direction = SW; break;
                    case E: IP->direction = *get_memory_edge(MP, &memory, &memory_rings) > 0 ? SE : NE; break;
                    case SE: IP->direction = NW; break;
                    case SW: IP->direction = W; break;
                    case W: IP->direction = E; break;
                    }
                    break;
                case '>':
                    switch (IP->direction) {
                    case NW: IP->direction = SE; break;
                    case NE: IP->direction = E; break;
                    case E: IP->direction = W; break;
                    case SE: IP->direction = E; break;
                    case SW: IP->direction = NE; break;
                    case W: IP->direction = *get_memory_edge(MP, &memory, &memory_rings) > 0 ? NW : SW; break;
                    }
                    break;

                // switches to the previous IP
                case '[': IP_index = modulo(IP_index + 1, 6); break;
                // switches to the next IP
                case ']': IP_index = modulo(IP_index - 1, 6); break;
                // takes the current memory edge modulo 6 and switches to the IP with that index.
                case '#': IP_index = modulo(*get_memory_edge(MP, &memory, &memory_rings), 6);

                // moves the MP to the left neighbour.
                case '{': move_mp(&MP, LEFT); break;
                // moves the MP to the right neighbour.
                case '}': move_mp(&MP, RIGHT); break;
                // moves the MP backwards and to the left. This is equivalent to =}=.
                case '"':
                    MP.direction = MP.direction == IN ? OUT : IN;
                    move_mp(&MP, RIGHT);
                    MP.direction = MP.direction == IN ? OUT : IN;
                    break;
                // moves the MP backwards and to the right. This is equivalent to ={=.
                case '\'':
                    MP.direction = MP.direction == IN ? OUT : IN;
                    move_mp(&MP, LEFT);
                    MP.direction = MP.direction == IN ? OUT : IN;
                    break;

                // reverses the direction of the MP. (This doesn't affect the current memory edge, but changes which
                // edges are considered the left and right neighbour.)
                case '=': MP.direction = MP.direction == IN ? OUT : IN; break;

                // moves the MP to the left neighbour if the current edge is zero or negative and to the right
                // neighbour if it's positive.
                case '^': move_mp(&MP, *get_memory_edge(MP, &memory, &memory_rings) <= 0 ? LEFT : RIGHT); break;

                // copies the value of left neighbour into the current edge if the current edge is zero or
                // negative and the value of the right neighbour if it's positive.
                case '&': {
                    int *edge = get_memory_edge(MP, &memory, &memory_rings);
                    *edge = *get_neighbor(MP, *edge <= 0 ? LEFT : RIGHT, &memory, &memory_rings);
                } break;
                }
            }
        }
        long np = IP->p + direction_offset[IP->direction].dp;
        long nq = IP->q + direction_offset[IP->direction].dq;
        long nr = -np - nq;
        if (labs(np) + labs(nq) + labs(nr) >= 2 * program_rings) {
            enum axis reflection;
            if (np == 0) {
                reflection = *get_memory_edge(MP, &memory, &memory_rings) > 0 ? Y : Z;
            } else if (nq == 0) {
                reflection = *get_memory_edge(MP, &memory, &memory_rings) > 0 ? Z : X;
            } else if (nr == 0) {
                reflection = *get_memory_edge(MP, &memory, &memory_rings) > 0 ? X : Y;
            } else if (nq * nr > 0) {
                reflection = X;
            } else if (nr * np > 0) {
                reflection = Y;
            } else if (np * nq > 0) {
                reflection = Z;
            }
            switch (reflection) {
            case X:
                np = -IP->p;
                nq = IP->p + IP->q;
                break;
            case Y:
                np = IP->p + IP->q;
                nq = -IP->q;
                break;
            case Z:
                np = -IP->q;
                nq = -IP->p;
                break;
            }
        }
        IP->p = np;
        IP->q = nq;
    } while (instruction->value != '@');

    free(memory);
    free(program);
}