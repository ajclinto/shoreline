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
    void changeColor()
    {
        QColorDialog *dialog = new QColorDialog(m_color, m_pb->parentWidget());
        connect(dialog, &QColorDialog::currentColorChanged, this, &PARAMETER_COLOR::setColor);
        connect(dialog, &QColorDialog::colorSelected, this, &PARAMETER_COLOR::setColor);

        QColor prev_color = m_color;
        connect(dialog, &QDialog::rejected, this, [this,prev_color](){ setColor(prev_color); });

        dialog->show();
    }
    void setColor(const QColor& color)
    {
        if (color != m_color)
        {
            m_color = color;
            m_pb->setStyleSheet("background-color: " + m_color.name() + "; border:none;");

            nlohmann::json color_json = {m_color.redF(), m_color.greenF(), m_color.blueF()};
            valueChanged(color_json);
        }
    }

private:
    QColor m_color;
    QPushButton *m_pb = nullptr;
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
