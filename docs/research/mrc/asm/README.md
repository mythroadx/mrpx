# ARM/Thumb 源码与格式研究样本

这是历史格式研究目录，不是自动编译输出目录。研究工作顺序见[研究指南](../../README.md)；下面原始结论保留其样本范围，不代表对所有平台已验证。

原始笔记：确认真机是小端字节序（展讯是大端）。

- [asm.mrp](../../../../tests/fixtures/mrp/asm.mrp)、[asm_thumb.mrp](../../../../tests/fixtures/mrp/asm_thumb.mrp)：原有 ARM/Thumb MRP，现作为固定回归输入单独保存
- asm.zip  mrp工程的完整源代码
- asm.s    mrp工程中使用到的汇编文件
- main.c   mrp工程中的C源代码
- cfunction.ext   mrp内提取的文件
- *.o    编译临时目标文件，不再保存在仓库中
- mr_cfunction.ext  在mrp编译过程中在%TMP%生成的文件
- mr_cfunction.fmt  在mrp编译过程中在%TMP%生成的文件,elf格式

[测试夹具说明](../../../../tests/fixtures/mrp/README.md)记录固定 MRP 的用途和文件哈希，不能当普通产物删除。现有 EXT、ELF/FMT 及反汇编文本保留为下面格式分析的原始样本，不由主工程构建生成；忽略规则只放行已有具名样本，不再放行所有 EXT 或目标文件。`asm.zip` 含有未在当前目录展开的 SDK 工程、构建配置和其他汇编源码，保留为原始源码归档。

以下结论只分析了一个样本得到，未经过多次验证：
```
在tmp目录下的临时文件mr_game.ext和最终在mrp中的game.ext相比
打包在mrp中的ext只是在文件开始处增加了8字节的MRPGCMAP

ext文件实际是fmt文件删头删尾，然后在开始处增加8字节MRPGCMAP
中间要保留下来的长度这样得到：
Program header在文件开头偏移量28的4字节记录
这个偏移量再加上16得到一个新的偏移量，在这个位置的4字节就是代码的长度
最后删除开始52字节(elf头)和保留长度之后的东西
```