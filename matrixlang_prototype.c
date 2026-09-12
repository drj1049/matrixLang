// MatrixLang - Initial Prototype
// Lexer + basic parser for matrix declarations and addition/multiplication

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum {
    TOK_ID, TOK_NUM, TOK_ASSIGN, TOK_PLUS, TOK_STAR,
    TOK_LBRACKET, TOK_RBRACKET, TOK_COMMA, TOK_LPAREN, TOK_RPAREN,
    TOK_PRINT, TOK_EOF, TOK_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char text[64];
} Token;

Token tokens[500];
int tokenCount = 0;

void addToken(TokenType type, const char *text) {
    tokens[tokenCount].type = type;
    strncpy(tokens[tokenCount].text, text, 63);
    tokenCount++;
}

const char* tokenName(TokenType t) {
    switch (t) {
        case TOK_ID: return "IDENTIFIER";
        case TOK_NUM: return "NUMBER";
        case TOK_ASSIGN: return "ASSIGN";
        case TOK_PLUS: return "PLUS";
        case TOK_STAR: return "STAR";
        case TOK_LBRACKET: return "LBRACKET";
        case TOK_RBRACKET: return "RBRACKET";
        case TOK_COMMA: return "COMMA";
        case TOK_LPAREN: return "LPAREN";
        case TOK_RPAREN: return "RPAREN";
        case TOK_PRINT: return "PRINT";
        case TOK_EOF: return "EOF";
        default: return "UNKNOWN";
    }
}

// ---------- LEXER ----------
void lex(const char *src) {
    int i = 0;
    int len = strlen(src);
    while (i < len) {
        char c = src[i];

        if (isspace(c)) { i++; continue; }

        if (isalpha(c)) {
            char buf[64]; int j = 0;
            while (i < len && (isalnum(src[i]) || src[i] == '_')) buf[j++] = src[i++];
            buf[j] = '\0';
            if (strcmp(buf, "print") == 0) addToken(TOK_PRINT, buf);
            else addToken(TOK_ID, buf);
            continue;
        }

        if (isdigit(c)) {
            char buf[64]; int j = 0;
            while (i < len && isdigit(src[i])) buf[j++] = src[i++];
            buf[j] = '\0';
            addToken(TOK_NUM, buf);
            continue;
        }

        switch (c) {
            case '=': addToken(TOK_ASSIGN, "="); i++; break;
            case '+': addToken(TOK_PLUS, "+"); i++; break;
            case '*': addToken(TOK_STAR, "*"); i++; break;
            case '[': addToken(TOK_LBRACKET, "["); i++; break;
            case ']': addToken(TOK_RBRACKET, "]"); i++; break;
            case ',': addToken(TOK_COMMA, ","); i++; break;
            case '(': addToken(TOK_LPAREN, "("); i++; break;
            case ')': addToken(TOK_RPAREN, ")"); i++; break;
            default:
                addToken(TOK_UNKNOWN, (char[2]){c, 0});
                i++;
                break;
        }
    }
    addToken(TOK_EOF, "EOF");
}

// ---------- BASIC PARSER ----------
// Confirms structure of: ID = matrixLiteral | ID = ID (+|*) ID | print(ID)
int pos = 0;

Token peek() { return tokens[pos]; }
Token advance() { return tokens[pos++]; }

int expect(TokenType type) {
    if (peek().type == type) { advance(); return 1; }
    printf("  [Parse error] expected %s but got %s ('%s')\n",
           tokenName(type), tokenName(peek().type), peek().text);
    return 0;
}

void parseMatrixLiteral() {
    expect(TOK_LBRACKET);
    while (peek().type != TOK_RBRACKET && peek().type != TOK_EOF) {
        if (peek().type == TOK_LBRACKET) {
            advance();
            while (peek().type != TOK_RBRACKET) { advance(); }
            advance(); // consume ]
        } else {
            advance();
        }
    }
    expect(TOK_RBRACKET);
}

void parseStatement() {
    if (peek().type == TOK_PRINT) {
        advance();
        expect(TOK_LPAREN);
        expect(TOK_ID);
        expect(TOK_RPAREN);
        printf("  Parsed: print statement\n");
        return;
    }

    if (peek().type == TOK_ID) {
        char lhs[64]; strcpy(lhs, peek().text);
        advance();
        expect(TOK_ASSIGN);

        if (peek().type == TOK_LBRACKET) {
            parseMatrixLiteral();
            printf("  Parsed: %s = <matrix literal>\n", lhs);
        } else if (peek().type == TOK_ID) {
            char op1[64]; strcpy(op1, peek().text);
            advance();
            if (peek().type == TOK_PLUS || peek().type == TOK_STAR) {
                const char *op = peek().type == TOK_PLUS ? "+" : "*";
                advance();
                char op2[64]; strcpy(op2, peek().text);
                expect(TOK_ID);
                printf("  Parsed: %s = %s %s %s\n", lhs, op1, op, op2);
            }
        }
    }
}

int main() {
    // Hardcoded sample MatrixLang program for this prototype demo
    const char *program =
        "A = [[1,2],[3,4]]\n"
        "B = [[5,6],[7,8]]\n"
        "C = A + B\n"
        "print(C)\n";

    printf("=== MatrixLang Prototype ===\n\n");
    printf("Input program:\n%s\n", program);

    lex(program);

    printf("Tokens generated:\n");
    for (int k = 0; k < tokenCount; k++) {
        printf("  %-12s %s\n", tokenName(tokens[k].type), tokens[k].text);
    }

    printf("\nParsing:\n");
    while (peek().type != TOK_EOF) {
        parseStatement();
    }

    printf("\nPrototype complete: lexer + parser successfully processed the sample program.\n");
    return 0;
}