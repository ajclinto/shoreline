/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include "parameter.h"

PARAMETER *PARAMETER::create_parameter(const nlohmann::json &json_p, QGridLayout *layout, QWidget *parent)
{
    int row = layout->rowCount();

    const auto &name = json_p["name"];
    layout->addWidget(new QLabel(QString(name.get<std::string>().c_str()), parent), row, 0, Qt::AlignRight);

    if (json_p["type"] == "float")
    {
        auto vector_it = json_p.find("vector_size");
        if (vector_it != json_p.end())
        {
            return new PARAMETER_VEC<double, QDoubleSpinBox>(json_p, layout, row, parent);
        }
        return new PARAMETER_FLOAT(json_p, layout, row, parent);
    }
    else if (json_p["type"] == "int")
    {
        auto vector_it = json_p.find("vector_size");
        if (vector_it != json_p.end())
        {
            return new PARAMETER_VEC<int, QSpinBox>(json_p, layout, row, parent);
        }
        return new PARAMETER_INT(json_p, layout, row, parent);
    }
    else if (json_p["type"] == "color")
    {
        return new PARAMETER_COLOR(json_p, layout, row, parent);
    }
    else if (json_p["type"] == "bool")
    {
        return new PARAMETER_BOOL(json_p, layout, row, parent);
    }
    else if (json_p["type"] == "string")
    {
        return new PARAMETER_STRING(json_p, layout, row, parent);
    }
    else
    {
        assert(0);
    }
}

PARAMETER_FLOAT::PARAMETER_FLOAT(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent)
{
    m_sb = new QDoubleSpinBox(parent);
    layout->addWidget(m_sb, row, 1);
    m_sb->setKeyboardTracking(false);
    m_sb->setMinimum(json_p["min"]);
    m_sb->setMaximum(json_p["max"]);
    m_sb->setDecimals(3);

    connect(m_sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PARAMETER::valueChanged);

    int imax = 1000;
    m_sl = new QSlider(Qt::Horizontal, parent);
    m_sl->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_sl, row, 2);
    m_sl->setMinimum(0);
    m_sl->setMaximum(imax);

    connect(m_sl, &QSlider::valueChanged, this,
            [this, imax](int value) { m_sb->setValue(m_sb->minimum() + (value / (double)imax)*(m_sb->maximum() - m_sb->minimum())); });
    connect(m_sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_sl,
            [this](double value) {
            m_sl->blockSignals(true);
            m_sl->setValue(static_cast<int>(m_sl->maximum() * (value - m_sb->minimum()) / (m_sb->maximum() - m_sb->minimum())));
            m_sl->blockSignals(false); }
           );
}

void PARAMETER_FLOAT::setValue(const nlohmann::json &value)
{
    double def = value;
    m_sb->blockSignals(true);
    m_sl->blockSignals(true);
    m_sb->setValue(def);
    m_sl->setValue(static_cast<int>(m_sl->maximum() * (def - m_sb->minimum()) / (m_sb->maximum() - m_sb->minimum())));
    m_sb->blockSignals(false);
    m_sl->blockSignals(false);
}

template <typename T, typename WIDGET_T>
PARAMETER_VEC<T, WIDGET_T>::PARAMETER_VEC(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent)
{
    QWidget *container = new QWidget(parent);
    QHBoxLayout *hlayout = new QHBoxLayout(container);
    hlayout->setContentsMargins(0, 0, 0, 0);
    hlayout->setSizeConstraint(QLayout::SetFixedSize);

    int vector_size = json_p["vector_size"];
    for (int i = 0; i < vector_size; i++)
    {
        auto *sb = new WIDGET_T(container);
        hlayout->addWidget(sb);
        sb->setKeyboardTracking(false);

        auto min_it = json_p.find("min");
        if (min_it != json_p.end()) sb->setMinimum(*min_it);

        auto max_it = json_p.find("max");
        if (max_it != json_p.end()) sb->setMaximum(*max_it);

        connect(sb, QOverload<T>::of(&WIDGET_T::valueChanged), this, [this](T)
            {
                nlohmann::json vec;
                for (int j = 0; j < (int)m_sb.size(); j++)
                    vec[j] = m_sb[j]->value();
                valueChanged(vec);
            }
        );

        m_sb.push_back(sb);
    }

    layout->addWidget(container, row, 1, 1, -1);
}

template <typename T, typename WIDGET_T>
void PARAMETER_VEC<T, WIDGET_T>::setValue(const nlohmann::json &value)
{
    for (int i = 0; i < (int)m_sb.size(); i++)
    {
        T def = value[i];
        m_sb[i]->blockSignals(true);
        m_sb[i]->setValue(def);
        m_sb[i]->blockSignals(false);
    }
}

static int from_log(int value, int imin, int imax)
{
    return std::min(imin*(1<<value), imax);
};
static int to_log(int value, int imin)
{
    int bits = 0;
    while (value > imin)
    {
        bits++;
        value >>= 1;
    }
    return bits;
};

PARAMETER_INT::PARAMETER_INT(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent)
{
    m_sb = new QSpinBox(parent);
    layout->addWidget(m_sb, row, 1);
    m_sb->setKeyboardTracking(false);
    m_sb->setMinimum(json_p["min"]);
    m_sb->setMaximum(json_p["max"]);

    connect(m_sb, QOverload<int>::of(&QSpinBox::valueChanged), this, &PARAMETER::valueChanged);

    m_sl = new QSlider(Qt::Horizontal, parent);
    m_sl->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_sl, row, 2);

    auto scale_it = json_p.find("scale");
    if (scale_it != json_p.end() && *scale_it == "log")
    {
        m_log_slider = true;

        int imin = json_p["min"];
        int imax = json_p["max"];

        m_sl->setMinimum(0);
        m_sl->setMaximum(to_log(imax, imin));

        connect(m_sl, &QSlider::valueChanged, this,
                [this,imin,imax](int value) { m_sb->setValue(from_log(value, imin, imax)); }
                );
        connect(m_sb, QOverload<int>::of(&QSpinBox::valueChanged), m_sl,
                [this,imin,imax](int value) { m_sl->blockSignals(true); m_sl->setValue(to_log(value, imin)); m_sl->blockSignals(false); }
               );
    }
    else
    {
        m_sl->setMinimum(json_p["min"]);
        m_sl->setMaximum(json_p["max"]);

        connect(m_sl, &QSlider::valueChanged, m_sb, &QSpinBox::setValue);
        connect(m_sb, QOverload<int>::of(&QSpinBox::valueChanged), m_sl,
                [this](int value) { m_sl->blockSignals(true); m_sl->setValue(value); m_sl->blockSignals(false); }
               );
    }
}

void PARAMETER_INT::setValue(const nlohmann::json &value)
{
    int def = value;
    m_sb->blockSignals(true);
    m_sl->blockSignals(true);
    m_sb->setValue(def);
    if (m_log_slider)
    {
        m_sl->setValue(to_log(def, m_sb->minimum()));
    }
    else
    {
        m_sl->setValue(def);
    }
    m_sb->blockSignals(false);
    m_sl->blockSignals(false);
}

PARAMETER_COLOR::PARAMETER_COLOR(const nlohmann::json &, QGridLayout *layout, int row, QWidget *parent)
{
    m_pb = new COLOR_PB(parent, m_color);

    connect( m_pb, &QPushButton::clicked, this, &PARAMETER_COLOR::changeColor);

    layout->addWidget(m_pb, row, 1);
}

void PARAMETER_COLOR::setValue(const nlohmann::json &value)
{
    blockSignals(true);
    QColor color;
    color.setRgbF(value[0], value[1], value[2]);
    setColor(color);
    blockSignals(false);
}

void PARAMETER_COLOR::changeColor()
{
    QColorDialog *dialog = new QColorDialog(m_color, m_pb->parentWidget());
    connect(dialog, &QColorDialog::currentColorChanged, this, &PARAMETER_COLOR::setColor);
    connect(dialog, &QColorDialog::colorSelected, this, &PARAMETER_COLOR::setColor);

    QColor prev_color = m_color;
    connect(dialog, &QDialog::rejected, this, [this,prev_color](){ setColor(prev_color); });

    dialog->show();
}

void PARAMETER_COLOR::setColor(const QColor& color)
{
    if (color != m_color)
    {
        m_color = color;
        m_pb->update_style();

        nlohmann::json color_json = {m_color.redF(), m_color.greenF(), m_color.blueF()};
        valueChanged(color_json);
    }
}

PARAMETER_BOOL::PARAMETER_BOOL(const nlohmann::json &, QGridLayout *layout, int row, QWidget *parent)
{
    m_cb = new QCheckBox("", parent);
    layout->addWidget(m_cb, row, 1);

    connect(m_cb, &QCheckBox::stateChanged, this,
            [&](int value) { valueChanged(value != 0); }
            );
}

void PARAMETER_BOOL::setValue(const nlohmann::json &value)
{
    bool def = value;
    m_cb->blockSignals(true);
    m_cb->setCheckState(def ? Qt::Checked : Qt::Unchecked);
    m_cb->blockSignals(false);
}

PARAMETER_STRING::PARAMETER_STRING(const nlohmann::json &json_p, QGridLayout *layout, int row, QWidget *parent)
{
    m_cb = new QComboBox(parent);
    for (std::string value : json_p["values"])
    {
        m_cb->addItem(QString(value.c_str()));
    }
    layout->addWidget(m_cb, row, 1);

    connect(m_cb, &QComboBox::currentTextChanged, this,
            [&](const QString &value) { valueChanged(value.toStdString()); }
            );
}

void PARAMETER_STRING::setValue(const nlohmann::json &value)
{
    std::string def = value;
    m_cb->blockSignals(true);
    // TODO
    //m_cb->setString(def);
    m_cb->blockSignals(false);
}

