#pragma once

// A FIFO drain can stop at a completed flip. Consume that notification before
// doing another drain, and also when this drain returns. A count-only present
// leaves the notification armed and presents the same frame again next time.
inline void haze_drain_frames(void (*drain)(), int (*take_flip)(),
                              void (*present)())
{
    if (take_flip()) present();
    drain();
    if (take_flip()) present();
}
