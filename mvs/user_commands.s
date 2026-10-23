        .include "helpers.inc"
        .include "ports.inc"
        .include "samples.inc"
        .include "snd_commands.inc"

        .area   CODE

cmd_jmptable::
        jp      gng_sound_ready
        jp      snd_command_01_prepare_for_rom_switch
        jp      snd_command_unused
        jp      snd_command_03_reset_driver
        generated_commands_jmptable_entries
        init_unused_cmd_jmptable

        generated_commands_implementation

; Command 0 is an application readiness probe. Unlike the NMI enqueue ACK,
; this reply proves initialization finished and the command main loop ran.
gng_sound_ready:
        ld      a, #0x7e
        out     (PORT_TO_68K), a
        ret
