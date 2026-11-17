/* Diagnostic pass-through thunks for the platform network manager vtable.
 * Each thunk marks its slot as called and jumps to the original entry without
 * touching argument registers (only r11). */
#include <stdint.h>
#include <stdio.h>
#define N 93
__attribute__((visibility("hidden"))) uintptr_t lce_vtable_trace_orig[N];
__attribute__((visibility("hidden"))) volatile unsigned char lce_vtable_trace_seen[N];
static unsigned char logged[N];
__asm__(".text\n"
".p2align 4\nlce_vtrace_0:\n movb $1, lce_vtable_trace_seen+0(%rip)\n jmp *lce_vtable_trace_orig+0(%rip)\n"
".p2align 4\nlce_vtrace_1:\n movb $1, lce_vtable_trace_seen+1(%rip)\n jmp *lce_vtable_trace_orig+8(%rip)\n"
".p2align 4\nlce_vtrace_2:\n movb $1, lce_vtable_trace_seen+2(%rip)\n jmp *lce_vtable_trace_orig+16(%rip)\n"
".p2align 4\nlce_vtrace_3:\n movb $1, lce_vtable_trace_seen+3(%rip)\n jmp *lce_vtable_trace_orig+24(%rip)\n"
".p2align 4\nlce_vtrace_4:\n movb $1, lce_vtable_trace_seen+4(%rip)\n jmp *lce_vtable_trace_orig+32(%rip)\n"
".p2align 4\nlce_vtrace_5:\n movb $1, lce_vtable_trace_seen+5(%rip)\n jmp *lce_vtable_trace_orig+40(%rip)\n"
".p2align 4\nlce_vtrace_6:\n movb $1, lce_vtable_trace_seen+6(%rip)\n jmp *lce_vtable_trace_orig+48(%rip)\n"
".p2align 4\nlce_vtrace_7:\n movb $1, lce_vtable_trace_seen+7(%rip)\n jmp *lce_vtable_trace_orig+56(%rip)\n"
".p2align 4\nlce_vtrace_8:\n movb $1, lce_vtable_trace_seen+8(%rip)\n jmp *lce_vtable_trace_orig+64(%rip)\n"
".p2align 4\nlce_vtrace_9:\n movb $1, lce_vtable_trace_seen+9(%rip)\n jmp *lce_vtable_trace_orig+72(%rip)\n"
".p2align 4\nlce_vtrace_10:\n movb $1, lce_vtable_trace_seen+10(%rip)\n jmp *lce_vtable_trace_orig+80(%rip)\n"
".p2align 4\nlce_vtrace_11:\n movb $1, lce_vtable_trace_seen+11(%rip)\n jmp *lce_vtable_trace_orig+88(%rip)\n"
".p2align 4\nlce_vtrace_12:\n movb $1, lce_vtable_trace_seen+12(%rip)\n jmp *lce_vtable_trace_orig+96(%rip)\n"
".p2align 4\nlce_vtrace_13:\n movb $1, lce_vtable_trace_seen+13(%rip)\n jmp *lce_vtable_trace_orig+104(%rip)\n"
".p2align 4\nlce_vtrace_14:\n movb $1, lce_vtable_trace_seen+14(%rip)\n jmp *lce_vtable_trace_orig+112(%rip)\n"
".p2align 4\nlce_vtrace_15:\n movb $1, lce_vtable_trace_seen+15(%rip)\n jmp *lce_vtable_trace_orig+120(%rip)\n"
".p2align 4\nlce_vtrace_16:\n movb $1, lce_vtable_trace_seen+16(%rip)\n jmp *lce_vtable_trace_orig+128(%rip)\n"
".p2align 4\nlce_vtrace_17:\n movb $1, lce_vtable_trace_seen+17(%rip)\n jmp *lce_vtable_trace_orig+136(%rip)\n"
".p2align 4\nlce_vtrace_18:\n movb $1, lce_vtable_trace_seen+18(%rip)\n jmp *lce_vtable_trace_orig+144(%rip)\n"
".p2align 4\nlce_vtrace_19:\n movb $1, lce_vtable_trace_seen+19(%rip)\n jmp *lce_vtable_trace_orig+152(%rip)\n"
".p2align 4\nlce_vtrace_20:\n movb $1, lce_vtable_trace_seen+20(%rip)\n jmp *lce_vtable_trace_orig+160(%rip)\n"
".p2align 4\nlce_vtrace_21:\n movb $1, lce_vtable_trace_seen+21(%rip)\n jmp *lce_vtable_trace_orig+168(%rip)\n"
".p2align 4\nlce_vtrace_22:\n movb $1, lce_vtable_trace_seen+22(%rip)\n jmp *lce_vtable_trace_orig+176(%rip)\n"
".p2align 4\nlce_vtrace_23:\n movb $1, lce_vtable_trace_seen+23(%rip)\n jmp *lce_vtable_trace_orig+184(%rip)\n"
".p2align 4\nlce_vtrace_24:\n movb $1, lce_vtable_trace_seen+24(%rip)\n jmp *lce_vtable_trace_orig+192(%rip)\n"
".p2align 4\nlce_vtrace_25:\n movb $1, lce_vtable_trace_seen+25(%rip)\n jmp *lce_vtable_trace_orig+200(%rip)\n"
".p2align 4\nlce_vtrace_26:\n movb $1, lce_vtable_trace_seen+26(%rip)\n jmp *lce_vtable_trace_orig+208(%rip)\n"
".p2align 4\nlce_vtrace_27:\n movb $1, lce_vtable_trace_seen+27(%rip)\n jmp *lce_vtable_trace_orig+216(%rip)\n"
".p2align 4\nlce_vtrace_28:\n movb $1, lce_vtable_trace_seen+28(%rip)\n jmp *lce_vtable_trace_orig+224(%rip)\n"
".p2align 4\nlce_vtrace_29:\n movb $1, lce_vtable_trace_seen+29(%rip)\n jmp *lce_vtable_trace_orig+232(%rip)\n"
".p2align 4\nlce_vtrace_30:\n movb $1, lce_vtable_trace_seen+30(%rip)\n jmp *lce_vtable_trace_orig+240(%rip)\n"
".p2align 4\nlce_vtrace_31:\n movb $1, lce_vtable_trace_seen+31(%rip)\n jmp *lce_vtable_trace_orig+248(%rip)\n"
".p2align 4\nlce_vtrace_32:\n movb $1, lce_vtable_trace_seen+32(%rip)\n jmp *lce_vtable_trace_orig+256(%rip)\n"
".p2align 4\nlce_vtrace_33:\n movb $1, lce_vtable_trace_seen+33(%rip)\n jmp *lce_vtable_trace_orig+264(%rip)\n"
".p2align 4\nlce_vtrace_34:\n movb $1, lce_vtable_trace_seen+34(%rip)\n jmp *lce_vtable_trace_orig+272(%rip)\n"
".p2align 4\nlce_vtrace_35:\n movb $1, lce_vtable_trace_seen+35(%rip)\n jmp *lce_vtable_trace_orig+280(%rip)\n"
".p2align 4\nlce_vtrace_36:\n movb $1, lce_vtable_trace_seen+36(%rip)\n jmp *lce_vtable_trace_orig+288(%rip)\n"
".p2align 4\nlce_vtrace_37:\n movb $1, lce_vtable_trace_seen+37(%rip)\n jmp *lce_vtable_trace_orig+296(%rip)\n"
".p2align 4\nlce_vtrace_38:\n movb $1, lce_vtable_trace_seen+38(%rip)\n jmp *lce_vtable_trace_orig+304(%rip)\n"
".p2align 4\nlce_vtrace_39:\n movb $1, lce_vtable_trace_seen+39(%rip)\n jmp *lce_vtable_trace_orig+312(%rip)\n"
".p2align 4\nlce_vtrace_40:\n movb $1, lce_vtable_trace_seen+40(%rip)\n jmp *lce_vtable_trace_orig+320(%rip)\n"
".p2align 4\nlce_vtrace_41:\n movb $1, lce_vtable_trace_seen+41(%rip)\n jmp *lce_vtable_trace_orig+328(%rip)\n"
".p2align 4\nlce_vtrace_42:\n movb $1, lce_vtable_trace_seen+42(%rip)\n jmp *lce_vtable_trace_orig+336(%rip)\n"
".p2align 4\nlce_vtrace_43:\n movb $1, lce_vtable_trace_seen+43(%rip)\n jmp *lce_vtable_trace_orig+344(%rip)\n"
".p2align 4\nlce_vtrace_44:\n movb $1, lce_vtable_trace_seen+44(%rip)\n jmp *lce_vtable_trace_orig+352(%rip)\n"
".p2align 4\nlce_vtrace_45:\n movb $1, lce_vtable_trace_seen+45(%rip)\n jmp *lce_vtable_trace_orig+360(%rip)\n"
".p2align 4\nlce_vtrace_46:\n movb $1, lce_vtable_trace_seen+46(%rip)\n jmp *lce_vtable_trace_orig+368(%rip)\n"
".p2align 4\nlce_vtrace_47:\n movb $1, lce_vtable_trace_seen+47(%rip)\n jmp *lce_vtable_trace_orig+376(%rip)\n"
".p2align 4\nlce_vtrace_48:\n movb $1, lce_vtable_trace_seen+48(%rip)\n jmp *lce_vtable_trace_orig+384(%rip)\n"
".p2align 4\nlce_vtrace_49:\n movb $1, lce_vtable_trace_seen+49(%rip)\n jmp *lce_vtable_trace_orig+392(%rip)\n"
".p2align 4\nlce_vtrace_50:\n movb $1, lce_vtable_trace_seen+50(%rip)\n jmp *lce_vtable_trace_orig+400(%rip)\n"
".p2align 4\nlce_vtrace_51:\n movb $1, lce_vtable_trace_seen+51(%rip)\n jmp *lce_vtable_trace_orig+408(%rip)\n"
".p2align 4\nlce_vtrace_52:\n movb $1, lce_vtable_trace_seen+52(%rip)\n jmp *lce_vtable_trace_orig+416(%rip)\n"
".p2align 4\nlce_vtrace_53:\n movb $1, lce_vtable_trace_seen+53(%rip)\n jmp *lce_vtable_trace_orig+424(%rip)\n"
".p2align 4\nlce_vtrace_54:\n movb $1, lce_vtable_trace_seen+54(%rip)\n jmp *lce_vtable_trace_orig+432(%rip)\n"
".p2align 4\nlce_vtrace_55:\n movb $1, lce_vtable_trace_seen+55(%rip)\n jmp *lce_vtable_trace_orig+440(%rip)\n"
".p2align 4\nlce_vtrace_56:\n movb $1, lce_vtable_trace_seen+56(%rip)\n jmp *lce_vtable_trace_orig+448(%rip)\n"
".p2align 4\nlce_vtrace_57:\n movb $1, lce_vtable_trace_seen+57(%rip)\n jmp *lce_vtable_trace_orig+456(%rip)\n"
".p2align 4\nlce_vtrace_58:\n movb $1, lce_vtable_trace_seen+58(%rip)\n jmp *lce_vtable_trace_orig+464(%rip)\n"
".p2align 4\nlce_vtrace_59:\n movb $1, lce_vtable_trace_seen+59(%rip)\n jmp *lce_vtable_trace_orig+472(%rip)\n"
".p2align 4\nlce_vtrace_60:\n movb $1, lce_vtable_trace_seen+60(%rip)\n jmp *lce_vtable_trace_orig+480(%rip)\n"
".p2align 4\nlce_vtrace_61:\n movb $1, lce_vtable_trace_seen+61(%rip)\n jmp *lce_vtable_trace_orig+488(%rip)\n"
".p2align 4\nlce_vtrace_62:\n movb $1, lce_vtable_trace_seen+62(%rip)\n jmp *lce_vtable_trace_orig+496(%rip)\n"
".p2align 4\nlce_vtrace_63:\n movb $1, lce_vtable_trace_seen+63(%rip)\n jmp *lce_vtable_trace_orig+504(%rip)\n"
".p2align 4\nlce_vtrace_64:\n movb $1, lce_vtable_trace_seen+64(%rip)\n jmp *lce_vtable_trace_orig+512(%rip)\n"
".p2align 4\nlce_vtrace_65:\n movb $1, lce_vtable_trace_seen+65(%rip)\n jmp *lce_vtable_trace_orig+520(%rip)\n"
".p2align 4\nlce_vtrace_66:\n movb $1, lce_vtable_trace_seen+66(%rip)\n jmp *lce_vtable_trace_orig+528(%rip)\n"
".p2align 4\nlce_vtrace_67:\n movb $1, lce_vtable_trace_seen+67(%rip)\n jmp *lce_vtable_trace_orig+536(%rip)\n"
".p2align 4\nlce_vtrace_68:\n movb $1, lce_vtable_trace_seen+68(%rip)\n jmp *lce_vtable_trace_orig+544(%rip)\n"
".p2align 4\nlce_vtrace_69:\n movb $1, lce_vtable_trace_seen+69(%rip)\n jmp *lce_vtable_trace_orig+552(%rip)\n"
".p2align 4\nlce_vtrace_70:\n movb $1, lce_vtable_trace_seen+70(%rip)\n jmp *lce_vtable_trace_orig+560(%rip)\n"
".p2align 4\nlce_vtrace_71:\n movb $1, lce_vtable_trace_seen+71(%rip)\n jmp *lce_vtable_trace_orig+568(%rip)\n"
".p2align 4\nlce_vtrace_72:\n movb $1, lce_vtable_trace_seen+72(%rip)\n jmp *lce_vtable_trace_orig+576(%rip)\n"
".p2align 4\nlce_vtrace_73:\n movb $1, lce_vtable_trace_seen+73(%rip)\n jmp *lce_vtable_trace_orig+584(%rip)\n"
".p2align 4\nlce_vtrace_74:\n movb $1, lce_vtable_trace_seen+74(%rip)\n jmp *lce_vtable_trace_orig+592(%rip)\n"
".p2align 4\nlce_vtrace_75:\n movb $1, lce_vtable_trace_seen+75(%rip)\n jmp *lce_vtable_trace_orig+600(%rip)\n"
".p2align 4\nlce_vtrace_76:\n movb $1, lce_vtable_trace_seen+76(%rip)\n jmp *lce_vtable_trace_orig+608(%rip)\n"
".p2align 4\nlce_vtrace_77:\n movb $1, lce_vtable_trace_seen+77(%rip)\n jmp *lce_vtable_trace_orig+616(%rip)\n"
".p2align 4\nlce_vtrace_78:\n movb $1, lce_vtable_trace_seen+78(%rip)\n jmp *lce_vtable_trace_orig+624(%rip)\n"
".p2align 4\nlce_vtrace_79:\n movb $1, lce_vtable_trace_seen+79(%rip)\n jmp *lce_vtable_trace_orig+632(%rip)\n"
".p2align 4\nlce_vtrace_80:\n movb $1, lce_vtable_trace_seen+80(%rip)\n jmp *lce_vtable_trace_orig+640(%rip)\n"
".p2align 4\nlce_vtrace_81:\n movb $1, lce_vtable_trace_seen+81(%rip)\n jmp *lce_vtable_trace_orig+648(%rip)\n"
".p2align 4\nlce_vtrace_82:\n movb $1, lce_vtable_trace_seen+82(%rip)\n jmp *lce_vtable_trace_orig+656(%rip)\n"
".p2align 4\nlce_vtrace_83:\n movb $1, lce_vtable_trace_seen+83(%rip)\n jmp *lce_vtable_trace_orig+664(%rip)\n"
".p2align 4\nlce_vtrace_84:\n movb $1, lce_vtable_trace_seen+84(%rip)\n jmp *lce_vtable_trace_orig+672(%rip)\n"
".p2align 4\nlce_vtrace_85:\n movb $1, lce_vtable_trace_seen+85(%rip)\n jmp *lce_vtable_trace_orig+680(%rip)\n"
".p2align 4\nlce_vtrace_86:\n movb $1, lce_vtable_trace_seen+86(%rip)\n jmp *lce_vtable_trace_orig+688(%rip)\n"
".p2align 4\nlce_vtrace_87:\n movb $1, lce_vtable_trace_seen+87(%rip)\n jmp *lce_vtable_trace_orig+696(%rip)\n"
".p2align 4\nlce_vtrace_88:\n movb $1, lce_vtable_trace_seen+88(%rip)\n jmp *lce_vtable_trace_orig+704(%rip)\n"
".p2align 4\nlce_vtrace_89:\n movb $1, lce_vtable_trace_seen+89(%rip)\n jmp *lce_vtable_trace_orig+712(%rip)\n"
".p2align 4\nlce_vtrace_90:\n movb $1, lce_vtable_trace_seen+90(%rip)\n jmp *lce_vtable_trace_orig+720(%rip)\n"
".p2align 4\nlce_vtrace_91:\n movb $1, lce_vtable_trace_seen+91(%rip)\n jmp *lce_vtable_trace_orig+728(%rip)\n"
".p2align 4\nlce_vtrace_92:\n movb $1, lce_vtable_trace_seen+92(%rip)\n jmp *lce_vtable_trace_orig+736(%rip)\n"
);
#define D(i) void lce_vtrace_##i(void);
void lce_vtrace_0(void); void lce_vtrace_1(void); void lce_vtrace_2(void); void lce_vtrace_3(void); void lce_vtrace_4(void); void lce_vtrace_5(void); void lce_vtrace_6(void); void lce_vtrace_7(void); void lce_vtrace_8(void); void lce_vtrace_9(void); void lce_vtrace_10(void); void lce_vtrace_11(void); void lce_vtrace_12(void); void lce_vtrace_13(void); void lce_vtrace_14(void); void lce_vtrace_15(void); void lce_vtrace_16(void); void lce_vtrace_17(void); void lce_vtrace_18(void); void lce_vtrace_19(void); void lce_vtrace_20(void); void lce_vtrace_21(void); void lce_vtrace_22(void); void lce_vtrace_23(void); void lce_vtrace_24(void); void lce_vtrace_25(void); void lce_vtrace_26(void); void lce_vtrace_27(void); void lce_vtrace_28(void); void lce_vtrace_29(void); void lce_vtrace_30(void); void lce_vtrace_31(void); void lce_vtrace_32(void); void lce_vtrace_33(void); void lce_vtrace_34(void); void lce_vtrace_35(void); void lce_vtrace_36(void); void lce_vtrace_37(void); void lce_vtrace_38(void); void lce_vtrace_39(void); void lce_vtrace_40(void); void lce_vtrace_41(void); void lce_vtrace_42(void); void lce_vtrace_43(void); void lce_vtrace_44(void); void lce_vtrace_45(void); void lce_vtrace_46(void); void lce_vtrace_47(void); void lce_vtrace_48(void); void lce_vtrace_49(void); void lce_vtrace_50(void); void lce_vtrace_51(void); void lce_vtrace_52(void); void lce_vtrace_53(void); void lce_vtrace_54(void); void lce_vtrace_55(void); void lce_vtrace_56(void); void lce_vtrace_57(void); void lce_vtrace_58(void); void lce_vtrace_59(void); void lce_vtrace_60(void); void lce_vtrace_61(void); void lce_vtrace_62(void); void lce_vtrace_63(void); void lce_vtrace_64(void); void lce_vtrace_65(void); void lce_vtrace_66(void); void lce_vtrace_67(void); void lce_vtrace_68(void); void lce_vtrace_69(void); void lce_vtrace_70(void); void lce_vtrace_71(void); void lce_vtrace_72(void); void lce_vtrace_73(void); void lce_vtrace_74(void); void lce_vtrace_75(void); void lce_vtrace_76(void); void lce_vtrace_77(void); void lce_vtrace_78(void); void lce_vtrace_79(void); void lce_vtrace_80(void); void lce_vtrace_81(void); void lce_vtrace_82(void); void lce_vtrace_83(void); void lce_vtrace_84(void); void lce_vtrace_85(void); void lce_vtrace_86(void); void lce_vtrace_87(void); void lce_vtrace_88(void); void lce_vtrace_89(void); void lce_vtrace_90(void); void lce_vtrace_91(void); void lce_vtrace_92(void);
static void (*const thunks[N])(void) = {lce_vtrace_0,lce_vtrace_1,lce_vtrace_2,lce_vtrace_3,lce_vtrace_4,lce_vtrace_5,lce_vtrace_6,lce_vtrace_7,lce_vtrace_8,lce_vtrace_9,lce_vtrace_10,lce_vtrace_11,lce_vtrace_12,lce_vtrace_13,lce_vtrace_14,lce_vtrace_15,lce_vtrace_16,lce_vtrace_17,lce_vtrace_18,lce_vtrace_19,lce_vtrace_20,lce_vtrace_21,lce_vtrace_22,lce_vtrace_23,lce_vtrace_24,lce_vtrace_25,lce_vtrace_26,lce_vtrace_27,lce_vtrace_28,lce_vtrace_29,lce_vtrace_30,lce_vtrace_31,lce_vtrace_32,lce_vtrace_33,lce_vtrace_34,lce_vtrace_35,lce_vtrace_36,lce_vtrace_37,lce_vtrace_38,lce_vtrace_39,lce_vtrace_40,lce_vtrace_41,lce_vtrace_42,lce_vtrace_43,lce_vtrace_44,lce_vtrace_45,lce_vtrace_46,lce_vtrace_47,lce_vtrace_48,lce_vtrace_49,lce_vtrace_50,lce_vtrace_51,lce_vtrace_52,lce_vtrace_53,lce_vtrace_54,lce_vtrace_55,lce_vtrace_56,lce_vtrace_57,lce_vtrace_58,lce_vtrace_59,lce_vtrace_60,lce_vtrace_61,lce_vtrace_62,lce_vtrace_63,lce_vtrace_64,lce_vtrace_65,lce_vtrace_66,lce_vtrace_67,lce_vtrace_68,lce_vtrace_69,lce_vtrace_70,lce_vtrace_71,lce_vtrace_72,lce_vtrace_73,lce_vtrace_74,lce_vtrace_75,lce_vtrace_76,lce_vtrace_77,lce_vtrace_78,lce_vtrace_79,lce_vtrace_80,lce_vtrace_81,lce_vtrace_82,lce_vtrace_83,lce_vtrace_84,lce_vtrace_85,lce_vtrace_86,lce_vtrace_87,lce_vtrace_88,lce_vtrace_89,lce_vtrace_90,lce_vtrace_91,lce_vtrace_92};
void lce_vtable_trace_install(uintptr_t *slots, unsigned count, const unsigned char *replaced) {
    if (count != N) return;
    for (unsigned i = 0; i < N; ++i) {
        if (replaced[i]) continue;
        lce_vtable_trace_orig[i] = slots[i];
        slots[i] = (uintptr_t)thunks[i];
    }
}
void mc195_remote_log(const char *);
/* Called from the plugin tick: logs each slot the first time it is used. */
void lce_vtable_trace_poll(void) {
    for (unsigned i = 0; i < N; ++i)
        if (lce_vtable_trace_seen[i] && !logged[i]) {
            logged[i] = 1;
            char line[64];
            snprintf(line, sizeof line, "Netmgr slot %u first call.\n", i);
            mc195_remote_log(line);
        }
}
