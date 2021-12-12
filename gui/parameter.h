/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef PARAMETER_H
#define PARAMETER_H

#include <QtGui>
#include <QGLWidget>
#include <QtOpenGL>
#include <nlohmann/json.hpp>

class PARAMETER : public QObject { Q_OBJECT
public:
    static PARAMETER *create_parameter(const nlohmann::json &json_p, QGridLayout *layout, QWidget *parent);

public slots:
    // Sets the value while disabling signals
    virtual void setValue(const nlohmann::json &value) = 0;

signals:
    void valueChanged(const nlohmann::json &value);
};

class PARAMETER_FLOAT : public PARAMETER {
public:
    PARAMETER_FLOAT(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent);

    virtual void setValue(const nlohmann::json &value) override;

private:
    QDoubleSpinBox *m_sb = nullptr;
    QSlider *m_sl = nullptr;
};

template <typename T, typename WIDGET_T>
class PARAMETER_VEC : public PARAMETER {
public:
    PARAMETER_VEC(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent);

    virtual void setValue(const nlohmann::json &value) override;

private:
    std::vector<WIDGET_T *> m_sb;
};

class PARAMETER_INT : public PARAMETER {
public:
    PARAMETER_INT(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent);

    virtual void setValue(const nlohmann::json &value) override;

private:
    QSpinBox *m_sb = nullptr;
    QSlider *m_sl = nullptr;
    bool m_log_slider = false;
};

class PARAMETER_COLOR : public PARAMETER { Q_OBJECT
public:
    PARAMETER_COLOR(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent);

    virtual void setValue(const nlohmann::json &value) override;

public slots:
    void changeColor();
    void setColor(const QColor& color);

private:
    class COLOR_PB : public QPushButton
    {
    public:
        COLOR_PB(QWidget *parent, QColor &color)
            : QPushButton(parent)
            , m_color(color) {}

        virtual void focusInEvent(QFocusEvent *) override { update_style(); }
        virtual void focusOutEvent(QFocusEvent *) override { update_style(); }

        void update_style()
        {
            QString style = "background-color: " + m_color.name();
            if (hasFocus())
            {
                style += "; border:1px solid black;";
            }
            else
            {
                style += "; border:1px solid " + m_color.name() + ";";
            }
            setStyleSheet(style);
        }

    private:
        QColor &m_color;
    };

    QColor m_color;
    COLOR_PB *m_pb = nullptr;
};

class PARAMETER_BOOL : public PARAMETER {
public:
    PARAMETER_BOOL(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent);

    virtual void setValue(const nlohmann::json &value) override;

private:
    QCheckBox *m_cb = nullptr;
};

class PARAMETER_STRING : public PARAMETER {
public:
    PARAMETER_STRING(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent);

    virtual void setValue(const nlohmann::json &value) override;

private:
    QComboBox *m_cb = nullptr;
};

#endif
