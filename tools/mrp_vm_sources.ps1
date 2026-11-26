param([Parameter(Mandatory = $true)][string]$VmDirectory)

$sources = @(
    'src\mr_api.c', 'src\mr_debug.c', 'src\mr_do.c', 'src\mr_dump.c',
    'src\mr_func.c', 'src\mr_gc.c', 'src\mr_mem.c', 'src\mr_object.c',
    'src\mr_opcodes.c', 'src\mr_state.c', 'src\mr_string.c', 'src\mr_table.c',
    'src\mr_tm.c', 'src\mr_undump.c', 'src\mr_vm.c', 'src\mr_zio.c',
    'src\parser\mr_code.c', 'src\parser\mr_lex.c', 'src\parser\mr_parser.c',
    'string.c', 'printf.c', 'strtol.c', 'strtoul.c'
)
$sources | ForEach-Object { Join-Path $VmDirectory $_ }
