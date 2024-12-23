
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRINGIFY(x) #x
#define STRINGIZE(x) STRINGIFY(x)

#define MEM_FMT_LEN 2 // digits per cell in memory debug view
#define MEM_FMT "%" STRINGIZE(MEM_FMT_LEN) "d"

enum axis { X, Y, Z };
enum direction { NW, NE, E, SE, SW, W };
enum neighbor { LEFT = -1, RIGHT = 1 };

struct program_cell {
    char value;
    bool debug;
};

// Memory is defined as an infinite hexagonal grid where each egde is a value.
// see https://www.redblobgames.com/grids/hexagons/ for terminology

// In this implementation, memory is indexed with the axial coordinates of a
// hexagonal grid. Each hexagon in the grid stores 3 values, one for each cubic
// axis.

typedef int memory_edge;

struct memory_cell {
    memory_edge value[3];
};

struct memory_pointer {
    long p, q;
    enum axis axis;
    enum { IN, OUT } direction;
};

// axial offests for each hexagonal direction
const struct {
    long dp, dq;
} direction_offset[] = {
    [NW] = { 0, -1},
    [NE] = {-1,  0},
    [ E] = {-1,  1}, 
    [SE] = { 0,  1},
    [SW] = { 1,  0},
    [ W] = { 1, -1},
};

const char *direction_name[] = {
    [NW] = "NORTH WEST", 
    [NE] = "NORTH EAST", 
    [ E] = "EAST",
    [SE] = "SOUTH EAST",
    [SW] = "SOUTH WEST",
    [ W] = "WEST",
};

const char *axis_name[] = {
    [X] = "X",
    [Y] = "Y",
    [Z] = "Z",
};

// mathematical modulus
long modulo(long a, long b) {
    const long result = a % labs(b);
    return (result >= 0 ? result : result + b) * (b >= 0 ? 1 : -1);
}

// convert x,y axial coordinates to index for sequentially stored rows along the z axis
ssize_t axial_to_index(long p, long q, long rings) {
    long x = p;
    long y = q;
    long z = -p - q;
    if (labs(x) + labs(y) + labs(z) > 2 * (rings - 1)) return -1;
    return (3 * rings * (rings - 1)) / 2
           + y + -z * (rings * 2 - 1)
           + z * (labs(z) + 1) / 2;
}

// convert x,y axial coordinate to a radial index
size_t axial_to_mem_index(long p, long q) {
    long x = p;
    long y = q;
    long z = -p - q;
    // The ring number is the hexagonal distance from the origin.
    // This is the same as half the manhattan distance in cubic coordinates.
    size_t ring = (labs(x) + labs(y) + labs(z)) / 2;
    size_t i = ring > 0 ? (3 * ring * (ring - 1) + 1) : 0;
    // find the clockwise offset from the closest corner of the ring
    if (x <= 0 && y < 0) i += ring * 0 + labs(x);
    if (y >= 0 && z > 0) i += ring * 1 + labs(y);
    if (z <= 0 && x < 0) i += ring * 2 + labs(z);
    if (x >= 0 && y > 0) i += ring * 3 + labs(x);
    if (y <= 0 && z < 0) i += ring * 4 + labs(y);
    if (z >= 0 && x > 0) i += ring * 5 + labs(z);
    return i;
}

// memory is allocated sequentially and grows outwards in rings
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

// get a pointer to the memory edge pointed to by ptr
memory_edge *get_memory_edge(struct memory_pointer ptr, struct memory_cell **memory, long *rings) {
    return &get_memory_cell(ptr.p, ptr.q, memory, rings)->value[ptr.axis];
}

// get a pointer to a neighbor of the edge pointed to by pointer
memory_edge *get_neighbor(struct memory_pointer ptr, enum neighbor neighbor, struct memory_cell **memory, long *rings) {
    long xyz[3] = {ptr.p, ptr.q, -ptr.p - ptr.q};
    enum axis neighbor_axis = modulo(ptr.axis + neighbor, 3);
    if (ptr.direction == OUT) {
        ++xyz[ptr.axis];
        --xyz[neighbor_axis];
    }
    struct memory_cell *cell = get_memory_cell(xyz[X], xyz[Y], memory, rings);
    return &cell->value[neighbor_axis];
}

// move memory pointer to its left or right neighbor
void move_mp(struct memory_pointer *ptr, enum neighbor neighbor) {
    long xyz[3] = {ptr->p, ptr->q, -ptr->p - ptr->q};
    enum axis neighbor_axis = modulo(ptr->axis + neighbor, 3);
    if (ptr->direction == OUT) {
        ++xyz[ptr->axis];
        --xyz[neighbor_axis];
        ptr->direction = IN;
    } else {
        ptr->direction = OUT;
    }
    ptr->axis = neighbor_axis;
    ptr->p = xyz[X];
    ptr->q = xyz[Y];
}

void print_program(struct program_cell *program, long program_rings, ssize_t ip_index[6]) {
    size_t i = 0;
    for (long z = -(program_rings - 1); z < program_rings; z++) {
        printf("%*s", labs(z), "");
        for (long x = 0; x < 2 * program_rings - 1 - labs(z); x++) {
            for (unsigned ip = 0; ip < 6; ip++) {
                if (i == ip_index[ip]) {
                    printf("\e[0;3%dm", ip + 1);
                    break;
                }
            }
            putchar(program[i].debug ? '`' : ' ');
            putchar(program[i].value);
            fputs("\e[0m", stdout);
            ++i;
        }
        putchar('\n');
    }
}

void print_memory(struct memory_cell *memory, long rings, const struct memory_pointer *ptr) {

    const long print_rings = 4; // how many rings around ptr to show
    const struct memory_cell oob = {0, 0, 0};
    const long ptr_z = -ptr->p - ptr->q;
    printf("[%ld rings allocated]\n", rings);

    for (long z = print_rings; z >= -print_rings; z--) {

        long x = print_rings;
        long y = -print_rings;
        if (z > 0)
            x -= z;
        if (z < 0)
            y -= z;

        for (long s = 0; s < labs(z); s++)
            printf("  %*s ", MEM_FMT_LEN, "");
        for (long p = x, q = y; labs(p) + labs(q) + labs(z) <= 2 * print_rings; --p, q++) {
            const struct memory_cell *cell;
            if (labs(ptr->p + p) + labs(ptr->q + q) + labs(ptr_z + z) <= 2 * rings)
                cell = get_memory_cell(ptr->p + p, ptr->q + q, &memory, &rings);
            else
                cell = &oob;
            printf("    \e[0;3%dm" MEM_FMT "\e[0m %*s ", (p == 0 && q == 0 && ptr->axis == Z) ? 1 : 0, cell->value[Z],
                   MEM_FMT_LEN, "");
        }
        putchar('\n');

        for (long s = 0; s < labs(z); s++)
            printf("  %*s ", MEM_FMT_LEN, "");
        for (long p = x, q = y; labs(p) + labs(q) + labs(z) <= 2 * print_rings; --p, q++) {
            const struct memory_cell *cell;
            if (labs(ptr->p + p) + labs(ptr->q + q) + labs(ptr_z + z) <= 2 * rings)
                cell = get_memory_cell(ptr->p + p, ptr->q + q, &memory, &rings);
            else
                cell = &oob;
            printf(". \e[0;3%dm" MEM_FMT "\e[0m ' \e[0;3%dm" MEM_FMT "\e[0m ",
                   (p == 0 && q == 0 && ptr->axis == X) ? 1 : 0, cell->value[X],
                   (p == 0 && q == 0 && ptr->axis == Y) ? 1 : 0, cell->value[Y]);
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
        {                   0, -(program_rings - 1),  E, false}, // NW
        {-(program_rings - 1),                    0, SE, false}, // NE
        {-(program_rings - 1), +(program_rings - 1), SW, false}, // E
        {                   0, +(program_rings - 1),  W, false}, // SE
        {+(program_rings - 1),                    0, NW, false}, // SW
        {+(program_rings - 1), -(program_rings - 1), NE, false}, // W
    };
    int IP_index = 0;

    long memory_rings = 1;
    struct memory_cell *memory = calloc(1, sizeof(struct memory_cell));
    struct memory_pointer MP = {0, 0, Z, OUT};

    bool force_debug = false;
    struct program_cell *instruction;
    while (true) {
        struct IP *IP = IPs + IP_index;
        if (IP->ignore_next) {
            IP->ignore_next = false;
        } else {
            instruction = program + axial_to_index(IP->p, IP->q, program_rings);
            if (instruction->debug || force_debug) {
                if (instruction->debug)
                    puts("break");
                printf("\nPaused on '%c'\n", instruction->value);
                ssize_t ips[6];
                for (unsigned ip = 0; ip < 6; ip++)
                    ips[ip] = axial_to_index(IPs[ip].p, IPs[ip].q, program_rings);
                print_program(program, program_rings, ips);
                printf("Active IP: %d\n", IP_index);
                int digits = log10(program_rings);
                for (int i = 0; i < 6; i++)
                    printf("IP \e[0;3%dm%d\e[0m (%+*ld, %+*ld) %s\n", i + 1, i, digits, IPs[i].p, digits, IPs[i].q,
                           direction_name[IPs[i].direction]);
                print_memory(memory, memory_rings, &MP);
                printf("MP: (%+ld, %+ld) %s %s = " MEM_FMT "\n", MP.p, MP.q, axis_name[MP.axis],
                       MP.direction == IN ? "INWARDS" : "OUTWARDS", *get_memory_edge(MP, &memory, &memory_rings));
            prompt:
                printf(": ");
                switch (getchar()) {
                case 's': force_debug = true; break;
                case 'c': force_debug = false; break;
                case 'q': goto done;
                default: goto prompt;
                }
            }
            if (isalpha(instruction->value)) { // set current memory edge to value
                *get_memory_edge(MP, &memory, &memory_rings) = instruction->value;
            
            } else if (isdigit(instruction->value)) {
                // multiply the current memory edge by 10 and add the corresponding digit.
                // if the current edge has a negative value, the digit is subtracted instead of added.
                memory_edge *edge = get_memory_edge(MP, &memory, &memory_rings);
                *edge *= 10;
                *edge += (*edge < 0 ? -1 : 1) * (instruction->value - '0');
            
            } else switch (instruction->value) {
                
                case '.': // no-op.
                    break;
                
                case '@': // terminates the program.
                    goto done;

                case ')': // increments the current memory edge. 
                    ++*get_memory_edge(MP, &memory, &memory_rings); 
                    break;

                case '(': // decrements the current memory edge.
                    --*get_memory_edge(MP, &memory, &memory_rings); 
                    break;

                case '+': // sets the current memory edge to the sum of the left and right neighbours.
                    *get_memory_edge(MP, &memory, &memory_rings) 
                        = *get_neighbor(MP,  LEFT, &memory, &memory_rings) 
                        + *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                case '-':  // sets the current memory edge to the difference of the left and right neighbours (left - right).
                    *get_memory_edge(MP, &memory, &memory_rings) 
                        = *get_neighbor(MP,  LEFT, &memory, &memory_rings) 
                        - *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                case '*':  // sets the current memory edge to the product of the left and right neighbours.
                    *get_memory_edge(MP, &memory, &memory_rings) 
                        = *get_neighbor(MP,  LEFT, &memory, &memory_rings) 
                        * *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                case ':': // sets the current memory edge to the quotient of the left and right neighbours (left / right).
                    *get_memory_edge(MP, &memory, &memory_rings) 
                        = *get_neighbor(MP,  LEFT, &memory, &memory_rings) 
                        / *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                case '%': // sets the current memory edge to the modulo of the left and right neighbours (left % right)
                    *get_memory_edge(MP, &memory, &memory_rings) 
                        = *get_neighbor(MP,  LEFT, &memory, &memory_rings) 
                        % *get_neighbor(MP, RIGHT, &memory, &memory_rings);
                    break;

                case '~': // multiplies the current memory edge by -1. 
                    *get_memory_edge(MP, &memory, &memory_rings) *= -1; 
                    break;

                
                case ',': // reads a single byte from STDIN and sets the current memory edge to its value, or -1 if EOF reached. 
                    *get_memory_edge(MP, &memory, &memory_rings) = getchar();
                    break;

                // reads and discards from STDIN until a digit, a - or a + is found. Then reads as many characters
                // as possible to form a valid (signed) decimal integer and sets the current memory edge to its
                // value. Returns 0 once EOF is reached.
                case '?': {
                    int ch = 0;
                    do {
                        ch = getchar();
                    } while (ch != EOF && !isdigit(ch) && ch != '+' && ch != '-');
                    memory_edge *edge = get_memory_edge(MP, &memory, &memory_rings);
                    *edge = 0;
                    if (ch != EOF) {
                        ungetc(ch, stdin);
                        scanf("%d", edge);
                    }
                }   break;

                case ';': // takes the current memory edge modulo 256 (positive) and writes the corresponding byte to STDOUT. 
                    printf("%c", (char)modulo(*get_memory_edge(MP, &memory, &memory_rings), 256)); 
                    break;

                case '!': // writes the decimal representation of the current memory edge to STDOUT. 
                    printf("%d", *get_memory_edge(MP, &memory, &memory_rings));
                    break;

                case '$': // is a jump. When executed, the IP completely ignores the next command in its current direction.
                    IP->ignore_next = true; 
                    break;

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
                    case NW: IP->direction =  E; break;
                    case NE: IP->direction = NE; break;
                    case  E: IP->direction = NW; break;
                    case SE: IP->direction =  W; break;
                    case SW: IP->direction = SW; break;
                    case  W: IP->direction = SE; break;
                    }
                    break;
                case '\\':
                    switch (IP->direction) {
                    case NW: IP->direction = NW; break;
                    case NE: IP->direction =  W; break;
                    case  E: IP->direction = SW; break;
                    case SE: IP->direction = SE; break;
                    case SW: IP->direction =  E; break;
                    case  W: IP->direction = NE; break;
                    }
                    break;
                case '_':
                    switch (IP->direction) {
                    case NW: IP->direction = SW; break;
                    case NE: IP->direction = SE; break;
                    case  E: IP->direction =  E; break;
                    case SE: IP->direction = NE; break;
                    case SW: IP->direction = NW; break;
                    case  W: IP->direction =  W; break;
                    }
                    break;
                case '|':
                    switch (IP->direction) {
                    case NW: IP->direction = NE; break;
                    case NE: IP->direction = NW; break;
                    case  E: IP->direction =  W; break;
                    case SE: IP->direction = SW; break;
                    case SW: IP->direction = SE; break;
                    case  W: IP->direction =  E; break;
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
                    case NW: IP->direction =  W; break;
                    case NE: IP->direction = SW; break;
                    case  E: IP->direction = *get_memory_edge(MP, &memory, &memory_rings) > 0 ? SE : NE; break;
                    case SE: IP->direction = NW; break;
                    case SW: IP->direction =  W; break;
                    case  W: IP->direction =  E; break;
                    }
                    break;
                case '>':
                    switch (IP->direction) {
                    case NW: IP->direction = SE; break;
                    case NE: IP->direction =  E; break;
                    case  E: IP->direction =  W; break;
                    case SE: IP->direction =  E; break;
                    case SW: IP->direction = NE; break;
                    case  W: IP->direction = *get_memory_edge(MP, &memory, &memory_rings) > 0 ? NW : SW; break;
                    }
                    break;

                
                case '[': // switches to the previous IP
                    IP_index = modulo(IP_index - 1, 6); 
                    break;

                case ']': // switches to the next IP 
                    IP_index = modulo(IP_index + 1, 6);
                    break;
  
                case '#': // takes the current memory edge modulo 6 and switches to the IP with that index.
                    IP_index = modulo(*get_memory_edge(MP, &memory, &memory_rings), 6); 
                    break;
                
                case '{': // moves the MP to the left neighbour. 
                    move_mp(&MP, LEFT); 
                    break;
                
                case '}': // moves the MP to the right neighbour.
                    move_mp(&MP, RIGHT); 
                    break;
                
                case '"': // moves the MP backwards and to the left. This is equivalent to =}=.
                    MP.direction = MP.direction == IN ? OUT : IN;
                    move_mp(&MP, RIGHT);
                    MP.direction = MP.direction == IN ? OUT : IN;
                    break;

                case '\'': // moves the MP backwards and to the right. This is equivalent to ={=.
                    MP.direction = MP.direction == IN ? OUT : IN;
                    move_mp(&MP, LEFT);
                    MP.direction = MP.direction == IN ? OUT : IN;
                    break;

                // reverses the direction of the MP. (This doesn't affect the current memory edge, but changes which
                // edges are considered the left and right neighbour.)
                case '=': 
                    MP.direction = MP.direction == IN ? OUT : IN;
                    break;

                // moves the MP to the left neighbour if the current edge is zero or negative and to the right
                // neighbour if it's positive.
                case '^': 
                    move_mp(&MP, *get_memory_edge(MP, &memory, &memory_rings) <= 0 ? LEFT : RIGHT);
                    break;

                // copies the value of left neighbour into the current edge if the current edge is zero or
                // negative and the value of the right neighbour if it's positive.
                case '&': {
                    int *edge = get_memory_edge(MP, &memory, &memory_rings);
                    *edge = *get_neighbor(MP, *edge <= 0 ? LEFT : RIGHT, &memory, &memory_rings);
                }   break;
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
    }

done:
    free(memory);
    free(program);

    return EXIT_SUCCESS;
}