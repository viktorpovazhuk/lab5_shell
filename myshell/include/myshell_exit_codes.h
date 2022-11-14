#ifndef MYCAT_ERRORS_H
#define MYCAT_ERRORS_H

enum ExitCodes {
    EFAILSET = 26,
    EWRONGPARAMS = 27,
    EFORKFAIL = 28,
    EEXECFAIL = 29,
    ESIGNALFAIL = 31,
    ENOTADIR=32,
    ENOTASCRIPT=33,

    EOTHER=34,
};

#endif //MYCAT_ERRORS_H
