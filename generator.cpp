/*
 * File:	generator.cpp
 *
 * Description:	This file contains the public and member function
 *		definitions for the code generator for Simple C.
 *
 *		Extra functionality:
 *		- putting all the global declarations at the end
 *		- prefix and suffix for globals (required on some systems)
 */

#include <vector>
#include <cassert>
#include <iostream>
#include "generator.h"
#include "machine.h"
#include "Tree.h"
#include <map>

using namespace std;

static int offset;
static string funcname, tab = "\t";
static string suffix(Expression *expr);
static ostream &operator<<(ostream &ostr, Expression *expr);
static map<string, Label> strings; // Objective 2-6

static Register *rax = new Register("%rax", "%eax", "%al");
static Register *rbx = new Register("%rbx", "%ebx", "%bl");
static Register *rcx = new Register("%rcx", "%ecx", "%cl");
static Register *rdx = new Register("%rdx", "%edx", "%dl");
static Register *rsi = new Register("%rsi", "%esi", "%sil");
static Register *rdi = new Register("%rdi", "%edi", "%dil");
static Register *r8 = new Register("%r8", "%r8d", "%r8b");
static Register *r9 = new Register("%r9", "%r9d", "%r9b");
static Register *r10 = new Register("%r10", "%r10d", "%r10b");
static Register *r11 = new Register("%r11", "%r11d", "%r11b");
static Register *r12 = new Register("%r12", "%r12d", "%r12b");
static Register *r13 = new Register("%r13", "%r13d", "%r13b");
static Register *r14 = new Register("%r14", "%r14d", "%r14b");
static Register *r15 = new Register("%r15", "%r15d", "%r15b");

static vector<Register *> parameters = {rdi, rsi, rdx, rcx, r8, r9};
static vector<Register *> registers = {rax, rdi, rsi, rdx, rcx, r8, r9, r10, r11};

/* Replaced functions for Phase 6. */

void assign(Expression *expr, Register *reg) // Objective 1
{
    if (expr != nullptr)
    {
        if (expr->reg != nullptr)
        {
            expr->reg->node = nullptr;
        }

        expr->reg = reg;
    }
    if (reg != nullptr)
    {
        if (reg->node != nullptr)
        {
            reg->node->reg = nullptr;
        }

        reg->node = expr;
    }
}

void load(Expression *expr, Register *reg) // Objective 1
{

    if (reg->node != expr)
    {
        if (reg->node != nullptr)
        {
            offset -= reg->node->type().size();
            reg->node->offset = offset;
            cout << "\tmov" << suffix(reg->node) << reg;
            cout << ", " << offset << "(%rbp)" << endl;
        }

        if (expr != nullptr)
        {
            unsigned size = expr->type().size();
            cout << "\tmov" << suffix(expr) << expr;
            cout << ", " << reg->name(size) << endl;
        }

        assign(expr, reg);
    }
}

static Register *getreg() // Objective 1
{
    for (auto reg : registers)
        if (reg->node == nullptr)
            return reg;
    load(nullptr, registers[0]);
    return registers[0];
}

/*
 * Function:	sign_extend_byte_arg (private)
 *
 * Description:	Sign extend a byte argument to 32 bits.  The Microsoft
 *		calling conventions explicitly state that parameters less
 *		than 64 bits long are not zero extended.  The System V
 *		conventions used for Unix-like systems do not specify what
 *		happens, but gcc and clang do sign extend, and clang
 *		apparently relies on it, but icc does not sign extend.
 *
 *		Writing to the 32 bit register will zero the upper 32-bits
 *		of the 64-bit register.  So in effect, an 8-bit value
 *		written to %al is sign extended into %eax but then zero
 *		extended into %rax.
 */

void sign_extend_byte_arg(Expression *arg)
{
    if (arg->type().size() == 1)
    {
        cout << tab << "movsbl" << tab << arg << ", ";
        cout << arg->reg->name(4) << endl;
    }
}

/*
 * Function:	suffix (private)
 *
 * Description:	Return the suffix for an opcode based on the given size.
 */

static string suffix(unsigned long size)
{
    return size == 1 ? "b\t" : (size == 4 ? "l\t" : "q\t");
}

/*
 * Function:	suffix (private)
 *
 * Description:	Return the suffix for an opcode based on the size of the
 *		given expression.
 */

static string suffix(Expression *expr)
{
    return suffix(expr->type().size());
}

/*
 * Function:	align (private)
 *
 * Description:	Return the number of bytes necessary to align the given
 *		offset on the stack.
 */

static int align(int offset)
{
    if (offset % STACK_ALIGNMENT == 0)
        return 0;

    return STACK_ALIGNMENT - (abs(offset) % STACK_ALIGNMENT);
}

/*
 * Function:	operator << (private)
 *
 * Description:	Convenience function for writing the operand of an
 *		expression using the output stream operator.
 */

static ostream &operator<<(ostream &ostr, Expression *expr)
{
    if (expr->reg != nullptr)
        return ostr << expr->reg;

    expr->operand(ostr);
    return ostr;
}

/*
 * Function:	Expression::operand
 *
 * Description:	Write an expression as an operand to the specified stream.
 */

void Expression::operand(ostream &ostr) const
{
    assert(offset != 0);
    ostr << offset << "(%rbp)";
}

/*
 * Function:	Identifier::operand
 *
 * Description:	Write an identifier as an operand to the specified stream.
 */

void Identifier::operand(ostream &ostr) const
{
    if (_symbol->offset == 0)
        ostr << global_prefix << _symbol->name() << global_suffix;
    else
        ostr << _symbol->offset << "(%rbp)";
}

/*
 * Function:	Number::operand
 *
 * Description:	Write a number as an operand to the specified stream.
 */

void Number::operand(ostream &ostr) const
{
    ostr << "$" << _value;
}

void String::operand(ostream &ostr) const // Objective 2-6
{
}
/*
 * Function:	Call::generate
 *
 * Description:	Generate code for a function call expression.
 *
 *		On a 64-bit platform, the stack needs to be aligned on a
 *		16-byte boundary.  So, if the stack will not be aligned
 *		after pushing any arguments, we first adjust the stack
 *		pointer.
 *
 *		Since all arguments are 8-bytes wide, we could simply do:
 *
 *		    if (args.size() > 6 && args.size() % 2 != 0)
 *			subq $8, %rsp
 */

void Call::generate()
{
    unsigned numBytes;

    /* Generate code for the arguments first. */

    numBytes = 0;

    for (int i = _args.size() - 1; i >= 0; i--)
        _args[i]->generate();

    /* Adjust the stack if necessary */

    if (_args.size() > NUM_PARAM_REGS)
    {
        numBytes = align((_args.size() - NUM_PARAM_REGS) * PARAM_ALIGNMENT);

        if (numBytes > 0)
            cout << tab << "subq" << tab << "$" << numBytes << ", %rsp" << endl;
    }

    /* Move the arguments into the correct registers or memory locations. */

    for (int i = _args.size() - 1; i >= 0; i--)
    {
        if (i >= NUM_PARAM_REGS)
        {
            numBytes += PARAM_ALIGNMENT;
            load(_args[i], rax);
            sign_extend_byte_arg(_args[i]);
            cout << tab << "pushq" << tab << "%rax" << endl;
        }
        else
        {
            load(_args[i], parameters[i]);
            sign_extend_byte_arg(_args[i]);
        }

        assign(_args[i], nullptr);
    }

    /* Call the function and then reclaim the stack space.  We only need to
       assign the number of floating point arguments passed in vector
       registers to %eax if the function being called takes a variable
       number of arguments. */

    for (auto reg : registers)
        load(nullptr, reg);

    if (_id->type().parameters()->variadic)
        cout << tab << "movl" << tab << "$0, %eax" << endl;

    cout << tab << "call" << tab << global_prefix << _id->name() << endl;

    if (numBytes > 0)
        cout << tab << "addq" << tab << "$" << numBytes << ", %rsp" << endl;

    assign(this, rax);
}

/*
 * Function:	Block::generate
 *
 * Description:	Generate code for this block, which simply means we
 *		generate code for each statement within the block.
 */

void Block::generate()
{
    for (auto stmt : _stmts)
    {
        stmt->generate();

        for (auto reg : registers)
            assert(reg->node == nullptr);
    }
}

/*
 * Function:	Simple::generate
 *
 * Description:	Generate code for a simple (expression) statement, which
 *		means simply generating code for the expression.
 */

void Simple::generate()
{
    _expr->generate();
    assign(_expr, nullptr);
}

/*
 * Function:	Function::generate
 *
 * Description:	Generate code for this function, which entails allocating
 *		space for local variables, then emitting our prologue, the
 *		body of the function, and the epilogue.
 */

void Function::generate()
{
    int param_offset;
    unsigned size;
    Symbols symbols;
    Types types;

    /* Assign offsets to the parameters and local variables. */

    param_offset = 2 * SIZEOF_REG;
    offset = param_offset;
    allocate(offset);

    /* Generate our prologue. */

    funcname = _id->name();
    cout << global_prefix << funcname << ":" << endl;
    cout << tab << "pushq" << tab << "%rbp" << endl;
    cout << tab << "movq" << tab << "%rsp, %rbp" << endl;
    cout << tab << "movl" << tab << "$" << funcname << ".size, %eax" << endl;
    cout << tab << "subq" << tab << "%rax, %rsp" << endl;

    /* Spill any parameters. */

    types = _id->type().parameters()->types;
    symbols = _body->declarations()->symbols();

    for (unsigned i = 0; i < NUM_PARAM_REGS; i++)
        if (i < types.size())
        {
            size = symbols[i]->type().size();
            cout << tab << "mov" << suffix(size) << parameters[i]->name(size);
            cout << ", " << symbols[i]->offset << "(%rbp)" << endl;
        }
        else
            break;

    /* Generate the body of this function. */

    _body->generate();

    /* Generate our epilogue. */

    cout << endl
         << global_prefix << funcname << ".exit:" << endl;
    cout << tab << "movq" << tab << "%rbp, %rsp" << endl;
    cout << tab << "popq" << tab << "%rbp" << endl;
    cout << tab << "ret" << endl
         << endl;

    offset -= align(offset - param_offset);
    cout << tab << ".set" << tab << funcname << ".size, " << -offset << endl;
    cout << tab << ".globl" << tab << global_prefix << funcname << endl
         << endl;
}

/*
 * Function:	generateGlobals
 *
 * Description:	Generate code for any global variable declarations.
 */

void generateGlobals(Scope *scope)
{
    const Symbols &symbols = scope->symbols();

    for (auto symbol : symbols)
        if (!symbol->type().isFunction())
        {
            cout << tab << ".comm" << tab << global_prefix << symbol->name();
            cout << ", " << symbol->type().size() << endl;
        }
}

/*
 * Function:	Assignment::generate
 *
 * Description:	Generate code for an assignment statement.
 *
 *		Modified Assignment::generate() for Phase 6.
 */

void Assignment::generate() // Objective 2 + Objective 2-5
{
    // assert(dynamic_cast<Number *>(_right));
    // assert(dynamic_cast<Identifier *>(_left));

    Expression *pointer; // To handle when LHS is a dereference

    _right->generate(); // Generate right

    if (_left->isDereference(pointer))
    {
        pointer->generate();

        if (pointer->reg == nullptr) // Load pointer (if not already in a register)
        {
            load(pointer, getreg());
        }
        if (_right->reg == nullptr) // Load right (if not already in a register)
        {
            load(_right, getreg());
        }

        cout << "\tmov" << suffix(_right); // Don’t forget to use suffix()!
        cout << ", " << "(" << pointer << ")" << endl;

        assign(_right, nullptr); // Unassign all registers afterwards (assign to nullptr)
        assign(_left, nullptr);
    }
    else
    {
        if (_right->reg == nullptr) // Load right (if not already in a register)
        {
            load(_right, getreg());
        }

        cout << "\tmov" << suffix(_right) << _right; // Don’t forget to use suffix()!
        cout << ", " << _left << endl;               // Move right into left

        assign(_right, nullptr); // Unassign all registers afterwards (assign to nullptr)
        assign(_left, nullptr);
    }
}

void Add::generate() // Objective 3
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
        load(_left, getreg());

    cout << "\tadd" << suffix(_left);        // Don’t forget to use suffix()!
    cout << _right << ", " << _left << endl; // Add right, left

    assign(_right, nullptr);  // Unassign right register
    assign(this, _left->reg); // Assign left register to this expression
}

void Subtract::generate() // Objective 3
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
        load(_left, getreg());

    cout << "\tsub" << suffix(_left);        // Don’t forget to use suffix()!
    cout << _right << ", " << _left << endl; // Subtract right, left

    assign(_right, nullptr);  // Unassign right register
    assign(this, _left->reg); // Assign left register to this expression
}

void Multiply::generate() // Objective 3
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
        load(_left, getreg());

    cout << "\tmul" << suffix(_left);        // Don’t forget to use suffix()!
    cout << _right << ", " << _left << endl; // Multiply right, left

    assign(_right, nullptr);  // Unassign right register
    assign(this, _left->reg); // Assign left register to this expression
}

void Divide::generate() // Objective 4
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left into rax (if not already in a register)
    {
        load(_left, rax);
    }

    load(nullptr, rdx); // Unload rdx

    if (_right->reg == nullptr) // Load right into rcx (if not already in a register)
    {
        load(_right, rcx);
    }

    if (_left->type().size() == 8) // Sign extend rax into rdx, if result is size 8...
    {
        cout << "\tcqto" << endl; // Use “cqto”
    }
    else
    {
        cout << "\tcltd" << endl; // Else use “cltd”
    }

    cout << "\tidiv" << suffix(_right); // Don’t forget to call Dr. Atkinson’s provided “suffix()”!
    cout << _right << endl;             // Divide right

    assign(_right, nullptr); // Unassign right register
    assign(_left, nullptr);  // Unassign left register

    assign(this, rax); // Assign this to rax
}

void Remainder::generate() // Objective 4
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left into rax (if not already in a register)
    {
        load(_left, rax);
    }

    load(nullptr, rdx); // Unload rdx

    if (_right->reg == nullptr) // Load right into rcx (if not already in a register)
    {
        load(_right, rcx);
    }

    if (_left->type().size() == 8) // Sign extend rax into rdx, if result is size 8...
    {
        cout << "\tcqto" << endl; // Use “cqto”
    }
    else
    {
        cout << "\tcltd" << endl; // Else use “cltd”
    }

    cout << "\tidiv" << suffix(_right); // Don’t forget to call Dr. Atkinson’s provided “suffix()”!
    cout << _right << endl;             // Remainder right

    assign(_right, nullptr); // Unassign right register
    assign(_left, nullptr);  // Unassign left register

    assign(this, rdx); // Assign this to rdx
}

void LessThan::generate() // Objective 5
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
    {
        load(_left, getreg());
    }

    cout << "\tcmp" << suffix(_left); // Compare left and right
    cout << _right << ", " << _left << endl;

    assign(_right, nullptr); // Unassign right register
    assign(_left, nullptr);  // Unassign left register

    assign(this, getreg()); // Assign this to a register

    string byteRegister = getreg()->byte();     // Store result of condition code in byte register
    cout << "\tsetl\t" << byteRegister << endl; // Condition opcode: setl – set if less than

    cout << "\tmovzb\t" << suffix(_left); // Zero-extend byte (using “movzb” + suffix)
    cout << byteRegister << ", " << this << endl;
}

void GreaterThan::generate() // Objective 5
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
    {
        load(_left, getreg());
    }

    cout << "\tcmp" << suffix(_left); // Compare left and right
    cout << _right << ", " << _left << endl;

    assign(_right, nullptr); // Unassign right register
    assign(_left, nullptr);  // Unassign left register

    assign(this, getreg()); // Assign this to a register

    string byteRegister = getreg()->byte();     // Store result of condition code in byte register
    cout << "\tsetg\t" << byteRegister << endl; // Condition opcode: setg – set if greater than

    cout << "\tmovzb\t" << suffix(_left); // Zero-extend byte (using “movzb” + suffix)
    cout << byteRegister << ", " << this << endl;
}

void LessOrEqual::generate() // Objective 5
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
    {
        load(_left, getreg());
    }

    cout << "\tcmp" << suffix(_left); // Compare left and right
    cout << _right << ", " << _left << endl;

    assign(_right, nullptr); // Unassign right register
    assign(_left, nullptr);  // Unassign left register

    assign(this, getreg()); // Assign this to a register

    string byteRegister = getreg()->byte();      // Store result of condition code in byte register
    cout << "\tsetle\t" << byteRegister << endl; // Condition opcode: setle – set if less than or equal

    cout << "\tmovzb\t" << suffix(_left); // Zero-extend byte (using “movzb” + suffix)
    cout << byteRegister << ", " << this << endl;
}

void GreaterOrEqual::generate() // Objective 5
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
    {
        load(_left, getreg());
    }

    cout << "\tcmp" << suffix(_left); // Compare left and right
    cout << _right << ", " << _left << endl;

    assign(_right, nullptr); // Unassign right register
    assign(_left, nullptr);  // Unassign left register

    assign(this, getreg()); // Assign this to a register

    string byteRegister = getreg()->byte();      // Store result of condition code in byte register
    cout << "\tsetge\t" << byteRegister << endl; // Condition opcode: setge – set if greater than or equal

    cout << "\tmovzb\t" << suffix(_left); // Zero-extend byte (using “movzb” + suffix)
    cout << byteRegister << ", " << this << endl;
}

void Equal::generate() // Objective 5
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
    {
        load(_left, getreg());
    }

    cout << "\tcmp" << suffix(_left); // Compare left and right
    cout << _right << ", " << _left << endl;

    assign(_right, nullptr); // Unassign right register
    assign(_left, nullptr);  // Unassign left register

    assign(this, getreg()); // Assign this to a register

    string byteRegister = getreg()->byte();     // Store result of condition code in byte register
    cout << "\tsete\t" << byteRegister << endl; // Condition opcode: sete – set if equal

    cout << "\tmovzb\t" << suffix(_left); // Zero-extend byte (using “movzb” + suffix)
    cout << byteRegister << ", " << this << endl;
}

void NotEqual::generate() // Objective 5
{
    _left->generate();  // Generate left
    _right->generate(); // Generate right

    if (_left->reg == nullptr) // Load left (if not already in a register)
    {
        load(_left, getreg());
    }

    cout << "\tcmp" << suffix(_left); // Compare left and right
    cout << _right << ", " << _left << endl;

    assign(_right, nullptr); // Unassign right register
    assign(_left, nullptr);  // Unassign left register

    assign(this, getreg()); // Assign this to a register

    string byteRegister = getreg()->byte();      // Store result of condition code in byte register
    cout << "\tsetne\t" << byteRegister << endl; // Condition opcode: setne – set if not equal

    cout << "\tmovzb\t" << suffix(_left); // Zero-extend byte (using “movzb” + suffix)
    cout << byteRegister << ", " << this << endl;
}

void Not::generate() // Objective 6
{
    _expr->generate(); // Start with generate expression

    if (_expr->reg == nullptr) // Load expression
    {
        load(_expr, getreg());
    }

    cout << "\tcmp" << suffix(_expr); // * is the result of suffix()
    cout << "$0, " << _expr << endl;  // cmp* $0, _expr

    string byteRegister = _expr->reg->byte(); // Store in byte register

    cout << "\tsete\t" << byteRegister << endl; // Perform sete reg->byte()

    cout << "\tmovzbl\t" << byteRegister << ", " << _expr->reg << endl; // Perform movzbl reg->byte(), reg

    assign(this, _expr->reg);
}

void Negate::generate() // Objective 6
{
    _expr->generate(); // Start with generate expression

    if (_expr->reg == nullptr) // Load expression
    {
        load(_expr, getreg());
    }

    cout << "\tneg" << suffix(_expr); // neg*, * is the result of suffix()
    cout << _expr << endl;

    assign(this, _expr->reg);
}

void Expression::test(const Label &label, bool ifTrue) // Objective 2-2
{
    generate();

    if (reg == nullptr)
    {
        load(this, getreg());
    }

    cout << "\tcmp" << suffix(this) << "$0, " << this << endl;
    cout << (ifTrue ? "\tjne\t" : "\tje\t") << label << endl;

    assign(this, nullptr);
}

void While::generate() // Objective 2-3
{
    Label loop, exit;

    cout << loop << ":" << endl;

    _expr->test(exit, false);
    _stmt->generate();

    cout << "\tjmp\t" << loop << endl;
    cout << exit << ":" << endl;
}

void Address::generate() // Objective 2-4
{
    Expression *pointer;

    if (_expr->isDereference(pointer))
    {
        pointer->generate();

        if (pointer->reg == nullptr)
        {
            load(pointer, getreg());
        }

        assign(this, pointer->reg);
    }
    else
    {
        assign(this, getreg());

        cout << "\tleaq\t" << _expr << ", " << this << endl;
    }
}

void Dereference::generate() // Objective 2-4
{
}

void Return::generate() // Objective 2-7
{
}

void Cast::generate() // Objective 2-8.1
{
}

void LogicalAnd::generate() // Objective 2-8.2
{
}

void LogicalOr::generate() // Objective 2-8.2
{
}

void For::generate() // Objective 2-8.2
{
}

void If::generate() // Objective 2-8.2
{
}

void Break::generate() // Objective 2-8.2
{
}