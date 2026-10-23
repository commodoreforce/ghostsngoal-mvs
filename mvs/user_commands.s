        .include "helpers.inc"
        .include "samples.inc"
        .include "snd_commands.inc"

        .area   CODE

cmd_jmptable::
        jp      snd_command_unused
        jp      snd_command_01_prepare_for_rom_switch
        jp      snd_command_unused
        jp      snd_command_03_reset_driver
        generated_commands_jmptable_entries
        init_unused_cmd_jmptable

        generated_commands_implementation
