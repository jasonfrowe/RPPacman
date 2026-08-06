#ifndef INPUT_H
#define INPUT_H

typedef struct input_actions_s {
    unsigned char up;
    unsigned char down;
    unsigned char left;
    unsigned char right;
    unsigned char land;
    unsigned char flip;
    unsigned char fire;
    unsigned char bomb;
    unsigned char start;
} input_actions_t;

void input_init(void);
void input_poll(input_actions_t *actions);
const input_actions_t *input_last_actions(void);

#endif // INPUT_H